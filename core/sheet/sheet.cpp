/*
 * sheet.cpp -- formatting, parsing, outline visibility, selection, editing,
 * undo and geometry for the task sheet.  See sheet.h for the contract.
 *
 * Everything here is portable C++20: no toolkit, no OS services, no locale.
 * The only dependency is core/view (values, record sets, tables), which is what
 * the importer and the reader bridge already produce.
 */

#include "sheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

#include "field_ref.h"

namespace core::sheet {
namespace {

/* ---- small text helpers ------------------------------------------------ */

bool is_digit(char ch) noexcept { return ch >= '0' && ch <= '9'; }

std::string_view trim_view(std::string_view text) noexcept {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
                           text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string trim_copy(std::string_view text) { return std::string(trim_view(text)); }

std::string lowercase_ascii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
    }
    return out;
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    out += text;
    out += '"';
    return out;
}

std::string pad2(int value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return std::string(buffer);
}

std::string pad4(int value) {
    char buffer[12];
    std::snprintf(buffer, sizeof(buffer), "%04d", value);
    return std::string(buffer);
}

/* ---- civil dates ------------------------------------------------------- */

/* Howard Hinnant's civil-date algorithms; the same ones core/view/group.cpp
 * and core/io/mpp_bridge.h use.  Kept local because core/sheet must not depend
 * on core/io. */
std::int64_t days_from_civil(int year, unsigned month, unsigned day) noexcept {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - static_cast<int>(era * 400));
    const unsigned doy =
        (153u * (month > 2u ? month - 3u : month + 9u) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

/* 0 = Sunday .. 6 = Saturday. */
unsigned weekday_from_days(std::int64_t serial) noexcept {
    return static_cast<unsigned>(serial >= -4 ? (serial + 4) % 7 : (serial + 5) % 7 + 6);
}

const char* const kWeekdayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

int days_in_month(int year, int month) noexcept {
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

/* ---- numbers ----------------------------------------------------------- */

/* Fixed-point text with trailing zeros and a bare trailing point removed.  The
 * text uses the configured decimal separator so every numeric cell agrees with
 * the cost cells; snprintf itself always writes '.', so the swap is here. */
std::string format_decimal(double value, int max_decimals, char decimal_separator) {
    if (!std::isfinite(value)) {
        return {};
    }
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%.*f", max_decimals, value);
    std::string text(buffer);
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        text = "0";
    }
    if (decimal_separator != '\0' && decimal_separator != '.') {
        for (char& ch : text) {
            if (ch == '.') {
                ch = decimal_separator;
            }
        }
    }
    return text;
}

/* Cost/number text with the configured thousands grouping, fixed decimals and
 * the currency symbol before the digits (after a minus sign). */
std::string format_grouped_fixed(double value, const SheetFormat& format) {
    if (!std::isfinite(value)) {
        return {};
    }
    int decimals = format.cost_decimals;
    if (decimals < 0 || decimals > 6) {
        decimals = 2;
    }
    const bool negative = value < 0.0;
    const double magnitude = negative ? -value : value;
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, magnitude);
    std::string text(buffer);

    std::string integer_part = text;
    std::string fraction_part;
    const std::size_t dot = text.find('.');
    if (dot != std::string::npos) {
        integer_part = text.substr(0, dot);
        fraction_part = text.substr(dot + 1);
    }

    std::string grouped;
    if (format.group_thousands && format.thousands_separator != '\0' &&
        integer_part.size() > 3) {
        std::size_t count = 0;
        for (std::size_t i = integer_part.size(); i > 0; --i) {
            if (count != 0 && count % 3 == 0) {
                grouped.push_back(format.thousands_separator);
            }
            grouped.push_back(integer_part[i - 1]);
            ++count;
        }
        std::reverse(grouped.begin(), grouped.end());
    } else {
        grouped = integer_part;
    }

    std::string out;
    if (negative && magnitude != 0.0) {
        out.push_back('-');
    }
    out += format.currency_symbol;
    out += grouped;
    if (decimals > 0) {
        out.push_back(format.decimal_separator);
        out += fraction_part;
    }
    return out;
}

std::string label_for_number(const std::string& number, const std::string& singular,
                             const std::string& plural) {
    return number == "1" ? singular : plural;
}

int sane_positive(int value, int fallback) noexcept { return value > 0 ? value : fallback; }

/* ---- scanning edit text ------------------------------------------------ */

struct NumberScan {
    double value = 0.0;
    bool integral = true;
    std::string error;
};

/* Signs, digits, one decimal separator ('.' always accepted, plus the
 * configured one) and, for the integer part only, properly placed thousands
 * separators.  No locale, no strtod. */
bool scan_decimal(std::string_view text, const SheetFormat& format, NumberScan& out) {
    out = NumberScan{};
    const std::string_view s = trim_view(text);
    if (s.empty()) {
        out.error = "expected a number";
        return false;
    }
    const char decimal_sep =
        format.decimal_separator == '\0' ? '.' : format.decimal_separator;
    const char group_sep = format.thousands_separator;

    std::size_t i = 0;
    bool negative = false;
    if (s[i] == '+' || s[i] == '-') {
        negative = s[i] == '-';
        ++i;
    }

    std::string digits;
    bool seen_digit = false;
    bool seen_point = false;
    std::size_t fraction_digits = 0;
    std::size_t fraction_nonzero = 0;

    for (; i < s.size(); ++i) {
        const char ch = s[i];
        if (is_digit(ch)) {
            digits.push_back(ch);
            seen_digit = true;
            if (seen_point) {
                ++fraction_digits;
                if (ch != '0') {
                    ++fraction_nonzero;
                }
            }
            continue;
        }
        if (ch == decimal_sep) {
            if (seen_point) {
                out.error = "more than one decimal separator";
                return false;
            }
            seen_point = true;
            continue;
        }
        if (ch == group_sep && group_sep != '\0' && group_sep != decimal_sep && !seen_point) {
            std::size_t j = i + 1;
            std::size_t count = 0;
            while (j < s.size() && is_digit(s[j])) {
                ++count;
                ++j;
            }
            if (!seen_digit || count != 3) {
                out.error = "misplaced thousands separator";
                return false;
            }
            /* The separator itself is dropped; the three digits are read by
             * the ordinary digit branch on the next iterations. */
            continue;
        }
        if (ch == '.') {
            if (seen_point) {
                out.error = "more than one decimal separator";
                return false;
            }
            seen_point = true;
            continue;
        }
        out.error = std::string("unexpected character ") + quote(std::string(1, ch));
        return false;
    }

    if (!seen_digit) {
        out.error = "expected a number, got " + quote(s);
        return false;
    }
    if (digits.size() > 18) {
        out.error = "number is too large";
        return false;
    }

    double value = 0.0;
    for (char ch : digits) {
        value = value * 10.0 + static_cast<double>(ch - '0');
    }
    if (fraction_digits > 0) {
        double divisor = 1.0;
        for (std::size_t k = 0; k < fraction_digits; ++k) {
            divisor *= 10.0;
        }
        value /= divisor;
    }
    if (negative) {
        value = -value;
    }
    out.value = value;
    out.integral = fraction_nonzero == 0;
    return true;
}

struct DurationScan {
    double value = 0.0;
    bool negative = false;
    std::string unit; /* lower-cased, may be empty */
    std::string error;
};

bool scan_duration(std::string_view text, const SheetFormat& format, DurationScan& out) {
    out = DurationScan{};
    const std::string_view s = trim_view(text);
    if (s.empty()) {
        out.error = "expected a duration such as 3d or 4h";
        return false;
    }
    const char decimal_sep =
        format.decimal_separator == '\0' ? '.' : format.decimal_separator;

    std::size_t i = 0;
    if (s[i] == '+' || s[i] == '-') {
        out.negative = s[i] == '-';
        ++i;
    }

    const std::size_t number_begin = i;
    bool seen_point = false;
    bool seen_digit = false;
    while (i < s.size()) {
        const char ch = s[i];
        if (is_digit(ch)) {
            seen_digit = true;
            ++i;
            continue;
        }
        if ((ch == '.' || ch == decimal_sep) && !seen_point) {
            seen_point = true;
            ++i;
            continue;
        }
        break;
    }
    if (!seen_digit) {
        out.error = "expected a number, got " + quote(s);
        return false;
    }

    std::string number(s.substr(number_begin, i - number_begin));
    for (char& ch : number) {
        if (ch == decimal_sep) {
            ch = '.';
        }
    }
    if (number.size() > 20) {
        out.error = "number is too large";
        return false;
    }
    double value = 0.0;
    double divisor = 1.0;
    bool after_point = false;
    for (char ch : number) {
        if (ch == '.') {
            after_point = true;
            continue;
        }
        value = value * 10.0 + static_cast<double>(ch - '0');
        if (after_point) {
            divisor *= 10.0;
        }
    }
    out.value = value / divisor;
    out.unit = lowercase_ascii(trim_view(s.substr(i)));
    return true;
}

enum class DurationUnit : std::uint8_t { Day = 0, Hour, Minute, Week, Month };

struct LoweredUnits {
    std::string day_singular;
    std::string day_plural;
    std::string hour_singular;
    std::string hour_plural;
    std::string minute_singular;
    std::string minute_plural;
    std::string week_singular;
    std::string week_plural;
    std::string month_singular;
    std::string month_plural;
};

LoweredUnits lower_units(const UnitLabels& units) {
    LoweredUnits out;
    out.day_singular = lowercase_ascii(units.day_singular);
    out.day_plural = lowercase_ascii(units.day_plural);
    out.hour_singular = lowercase_ascii(units.hour_singular);
    out.hour_plural = lowercase_ascii(units.hour_plural);
    out.minute_singular = lowercase_ascii(units.minute_singular);
    out.minute_plural = lowercase_ascii(units.minute_plural);
    out.week_singular = lowercase_ascii(units.week_singular);
    out.week_plural = lowercase_ascii(units.week_plural);
    out.month_singular = lowercase_ascii(units.month_singular);
    out.month_plural = lowercase_ascii(units.month_plural);
    return out;
}

bool unit_matches(const std::string& unit, std::string_view candidate) {
    return !candidate.empty() && unit == candidate;
}

bool duration_unit_from_text(const std::string& unit, const LoweredUnits& labels,
                             DurationUnit& out) {
    if (unit.empty()) {
        out = DurationUnit::Day;
        return true;
    }
    if (unit_matches(unit, "d") || unit_matches(unit, "day") || unit_matches(unit, "days") ||
        unit_matches(unit, labels.day_singular) || unit_matches(unit, labels.day_plural)) {
        out = DurationUnit::Day;
        return true;
    }
    if (unit_matches(unit, "h") || unit_matches(unit, "hr") || unit_matches(unit, "hrs") ||
        unit_matches(unit, "hour") || unit_matches(unit, "hours") ||
        unit_matches(unit, labels.hour_singular) || unit_matches(unit, labels.hour_plural)) {
        out = DurationUnit::Hour;
        return true;
    }
    if (unit_matches(unit, "m") || unit_matches(unit, "min") || unit_matches(unit, "mins") ||
        unit_matches(unit, "minute") || unit_matches(unit, "minutes") ||
        unit_matches(unit, labels.minute_singular) ||
        unit_matches(unit, labels.minute_plural)) {
        out = DurationUnit::Minute;
        return true;
    }
    if (unit_matches(unit, "w") || unit_matches(unit, "wk") || unit_matches(unit, "wks") ||
        unit_matches(unit, "week") || unit_matches(unit, "weeks") ||
        unit_matches(unit, labels.week_singular) || unit_matches(unit, labels.week_plural)) {
        out = DurationUnit::Week;
        return true;
    }
    if (unit_matches(unit, "mo") || unit_matches(unit, "mos") || unit_matches(unit, "mon") ||
        unit_matches(unit, "mons") || unit_matches(unit, "month") ||
        unit_matches(unit, "months") || unit_matches(unit, labels.month_singular) ||
        unit_matches(unit, labels.month_plural)) {
        out = DurationUnit::Month;
        return true;
    }
    return false;
}

std::int64_t ticks_per_unit(DurationUnit unit, const SheetFormat& format) noexcept {
    const std::int64_t hour = core::view::kTicksPerHour;
    const std::int64_t day = hour * sane_positive(format.hours_per_day, 8);
    switch (unit) {
        case DurationUnit::Hour: return hour;
        case DurationUnit::Minute: return core::view::kTicksPerMinute;
        case DurationUnit::Week: return day * sane_positive(format.days_per_week, 5);
        case DurationUnit::Month: return day * sane_positive(format.days_per_month, 20);
        case DurationUnit::Day:
        default: return day;
    }
}

/* ---- date parsing ------------------------------------------------------ */

bool parse_small_uint(std::string_view text, int& out) noexcept {
    const std::string_view trimmed = trim_view(text);
    if (trimmed.empty() || trimmed.size() > 4) {
        return false;
    }
    int value = 0;
    for (char ch : trimmed) {
        if (!is_digit(ch)) {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    out = value;
    return true;
}

bool parse_time_of_day(std::string_view text, int& hour, int& minute, int& second) noexcept {
    const std::string_view trimmed = trim_view(text);
    int h = 0;
    int m = 0;
    int s = 0;
    std::size_t i = 0;
    auto read_two = [&](int& value) -> bool {
        if (i + 2 > trimmed.size() || !is_digit(trimmed[i]) || !is_digit(trimmed[i + 1])) {
            return false;
        }
        value = (trimmed[i] - '0') * 10 + (trimmed[i + 1] - '0');
        i += 2;
        return true;
    };
    if (!read_two(h) || i >= trimmed.size() || trimmed[i] != ':') {
        return false;
    }
    ++i;
    if (!read_two(m)) {
        return false;
    }
    if (i < trimmed.size() && trimmed[i] == ':') {
        ++i;
        if (!read_two(s)) {
            return false;
        }
    }
    if (i != trimmed.size()) {
        return false;
    }
    if (h > 23 || m > 59 || s > 59) {
        return false;
    }
    hour = h;
    minute = m;
    second = s;
    return true;
}

/* The parse side of DateFormat; see the documentation on parse_edit_text. */
bool parse_date_text(std::string_view text, const DateFormat& format,
                     core::view::DateValue& out, std::string& error) {
    out = core::view::DateValue{};
    const std::string trimmed = trim_copy(text);
    if (trimmed.empty()) {
        error = "expected a date";
        return false;
    }

    /* ISO is unambiguous, so it is accepted whatever the display order is. */
    core::view::DateValue iso;
    if (core::view::parse_iso_datetime(trimmed, iso)) {
        out = iso;
        return true;
    }

    std::string date_part = trimmed;
    std::string time_part;
    const std::size_t separator = trimmed.find_first_of("T ");
    if (separator != std::string::npos) {
        date_part = trimmed.substr(0, separator);
        time_part = trim_copy(trimmed.substr(separator + 1));
    }

    std::vector<std::string> fields;
    std::string current;
    for (char ch : date_part) {
        if (ch == '/' || ch == '-' || ch == '.') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    if (fields.size() != 3) {
        error = "expected a date like 20/10/26, got " + quote(trimmed);
        return false;
    }

    int numbers[3] = {0, 0, 0};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!parse_small_uint(fields[i], numbers[i])) {
            error = "expected a numeric date, got " + quote(trimmed);
            return false;
        }
    }

    /* A four-digit first or last field pins the year regardless of the display
     * order; otherwise the configured order decides which field is the year. */
    const std::size_t first_digits = trim_view(fields[0]).size();
    const std::size_t last_digits = trim_view(fields[2]).size();
    const bool first_is_year = first_digits == 4;
    const bool last_is_year = last_digits == 4;

    DateOrder order = format.order;
    if (first_is_year && !last_is_year) {
        order = DateOrder::YearMonthDay;
    } else if (last_is_year && !first_is_year) {
        order = format.order == DateOrder::YearMonthDay ? DateOrder::DayMonthYear : format.order;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    switch (order) {
        case DateOrder::DayMonthYear:
            day = numbers[0];
            month = numbers[1];
            year = numbers[2];
            break;
        case DateOrder::MonthDayYear:
            month = numbers[0];
            day = numbers[1];
            year = numbers[2];
            break;
        case DateOrder::YearMonthDay:
            year = numbers[0];
            month = numbers[1];
            day = numbers[2];
            break;
    }

    const std::size_t year_index = order == DateOrder::YearMonthDay ? 0 : 2;
    const std::size_t year_digits = trim_view(fields[year_index]).size();
    if (year_digits <= 2) {
        year = year <= 29 ? 2000 + year : 1900 + year;
    } else if (year_digits == 3) {
        error = "expected a two- or four-digit year, got " + quote(trimmed);
        return false;
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!time_part.empty() && !parse_time_of_day(time_part, hour, minute, second)) {
        error = "expected a time like 17:00, got " + quote(time_part);
        return false;
    }

    out.year = static_cast<std::int32_t>(year);
    out.month = static_cast<std::uint8_t>(month);
    out.day = static_cast<std::uint8_t>(day);
    out.hour = static_cast<std::uint8_t>(hour);
    out.minute = static_cast<std::uint8_t>(minute);
    out.second = static_cast<std::uint8_t>(second);
    if (!out.valid()) {
        if (month < 1 || month > 12) {
            error = "month " + std::to_string(month) + " is out of range";
        } else if (day < 1 || day > days_in_month(year, month)) {
            error = "day " + std::to_string(day) + " is out of range for month " +
                    std::to_string(month);
        } else {
            error = "time is out of range";
        }
        return false;
    }
    return true;
}

std::string format_weekday(const core::view::DateValue& date) {
    const std::int64_t serial = days_from_civil(date.year, date.month, date.day);
    return kWeekdayNames[weekday_from_days(serial) % 7];
}

bool values_equal(const Value& a, const Value& b) noexcept {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
        case core::view::ValueKind::Null: return true;
        case core::view::ValueKind::Bool: return a.boolean == b.boolean;
        case core::view::ValueKind::Integer:
        case core::view::ValueKind::Duration: return a.integer == b.integer;
        case core::view::ValueKind::Number: return a.number == b.number;
        case core::view::ValueKind::Text: return a.text == b.text;
        case core::view::ValueKind::Date: return a.date == b.date;
    }
    return false;
}

std::vector<SheetColumn> columns_from_table(const core::view::TableDefinition& table) {
    std::vector<SheetColumn> columns;
    columns.reserve(table.columns.size());
    for (const core::view::Column& source : table.columns) {
        SheetColumn column;
        column.field = source.field;
        column.title = source.title;
        column.width = source.width;
        column.align = source.align;
        columns.push_back(std::move(column));
    }
    return columns;
}

bool is_numeric_kind(CellKind kind) noexcept {
    switch (kind) {
        case CellKind::Integer:
        case CellKind::Number:
        case CellKind::Duration:
        case CellKind::Cost:
        case CellKind::Percent: return true;
        default: return false;
    }
}

}  // namespace

/* ---- enum names -------------------------------------------------------- */

const char* cell_kind_name(CellKind kind) noexcept {
    switch (kind) {
        case CellKind::Auto: return "auto";
        case CellKind::Text: return "text";
        case CellKind::Integer: return "integer";
        case CellKind::Number: return "number";
        case CellKind::Duration: return "duration";
        case CellKind::Date: return "date";
        case CellKind::Percent: return "percent";
        case CellKind::Cost: return "cost";
        case CellKind::Bool: return "bool";
    }
    return "auto";
}

bool cell_kind_from_name(std::string_view name, CellKind& out) noexcept {
    const std::string lowered = lowercase_ascii(trim_view(name));
    if (lowered == "auto") { out = CellKind::Auto; return true; }
    if (lowered == "text") { out = CellKind::Text; return true; }
    if (lowered == "integer") { out = CellKind::Integer; return true; }
    if (lowered == "number") { out = CellKind::Number; return true; }
    if (lowered == "duration") { out = CellKind::Duration; return true; }
    if (lowered == "date") { out = CellKind::Date; return true; }
    if (lowered == "percent") { out = CellKind::Percent; return true; }
    if (lowered == "cost") { out = CellKind::Cost; return true; }
    if (lowered == "bool") { out = CellKind::Bool; return true; }
    return false;
}

CellKind value_cell_kind(const Value& value) noexcept {
    using core::view::ValueKind;
    switch (value.kind) {
        case ValueKind::Bool: return CellKind::Bool;
        case ValueKind::Integer: return CellKind::Integer;
        case ValueKind::Number: return CellKind::Number;
        case ValueKind::Text: return CellKind::Text;
        case ValueKind::Duration: return CellKind::Duration;
        case ValueKind::Date: return CellKind::Date;
        case ValueKind::Null:
        default: return CellKind::Text;
    }
}

CellKind field_cell_kind(FieldId field) noexcept {
    switch (core::view::field_value_kind(field)) {
        case core::view::ValueKind::Bool: return CellKind::Bool;
        case core::view::ValueKind::Integer: return CellKind::Integer;
        case core::view::ValueKind::Number: return CellKind::Number;
        case core::view::ValueKind::Text: return CellKind::Text;
        case core::view::ValueKind::Duration: return CellKind::Duration;
        case core::view::ValueKind::Date: return CellKind::Date;
        case core::view::ValueKind::Null:
        default: return CellKind::Text;
    }
}

const char* undo_kind_name(UndoKind kind) noexcept {
    switch (kind) {
        case UndoKind::Cell: return "cell";
        case UndoKind::Typing: return "typing";
        case UndoKind::Transaction: return "transaction";
        case UndoKind::Paste: return "paste";
    }
    return "cell";
}

/* ---- formatting -------------------------------------------------------- */

std::string format_duration_text(std::int64_t ticks, const SheetFormat& format) {
    const int hours_per_day = sane_positive(format.hours_per_day, 8);
    const std::int64_t ticks_per_hour = core::view::kTicksPerHour;
    const std::int64_t ticks_per_minute = core::view::kTicksPerMinute;
    const std::int64_t ticks_per_day = ticks_per_hour * hours_per_day;

    const bool negative = ticks < 0;
    const std::uint64_t magnitude =
        negative ? static_cast<std::uint64_t>(-(ticks + 1)) + 1u
                 : static_cast<std::uint64_t>(ticks);

    std::string number;
    std::string label;
    if (magnitude == 0) {
        number = "0";
        label = format.units.day_plural;
    } else if (magnitude >= static_cast<std::uint64_t>(ticks_per_day)) {
        number = format_decimal(static_cast<double>(magnitude) /
                                    static_cast<double>(ticks_per_day),
                                2, format.decimal_separator);
        label = label_for_number(number, format.units.day_singular, format.units.day_plural);
    } else if (magnitude >= static_cast<std::uint64_t>(ticks_per_hour)) {
        number = format_decimal(static_cast<double>(magnitude) /
                                    static_cast<double>(ticks_per_hour),
                                2, format.decimal_separator);
        label = label_for_number(number, format.units.hour_singular, format.units.hour_plural);
    } else {
        number = format_decimal(static_cast<double>(magnitude) /
                                    static_cast<double>(ticks_per_minute),
                                2, format.decimal_separator);
        label = label_for_number(number, format.units.minute_singular,
                                 format.units.minute_plural);
    }

    std::string out;
    if (negative && magnitude != 0) {
        out.push_back('-');
    }
    out += number;
    if (!label.empty()) {
        out.push_back(' ');
        out += label;
    }
    return out;
}

std::string format_date_text(const core::view::DateValue& date, const SheetFormat& format) {
    if (!date.valid()) {
        return {};
    }
    std::string out;
    if (format.date.show_weekday) {
        out += format_weekday(date);
        out.push_back(' ');
    }

    const std::string year_text =
        format.date.two_digit_year ? pad2(((date.year % 100) + 100) % 100) : pad4(date.year);
    const std::string month_text = pad2(date.month);
    const std::string day_text = pad2(date.day);
    const char separator = format.date.separator == '\0' ? '/' : format.date.separator;

    switch (format.date.order) {
        case DateOrder::DayMonthYear:
            out += day_text;
            out.push_back(separator);
            out += month_text;
            out.push_back(separator);
            out += year_text;
            break;
        case DateOrder::MonthDayYear:
            out += month_text;
            out.push_back(separator);
            out += day_text;
            out.push_back(separator);
            out += year_text;
            break;
        case DateOrder::YearMonthDay:
            out += year_text;
            out.push_back(separator);
            out += month_text;
            out.push_back(separator);
            out += day_text;
            break;
    }

    if (format.date.show_time_of_day && date.has_time_of_day()) {
        out.push_back(' ');
        out += pad2(date.hour);
        out.push_back(':');
        out += pad2(date.minute);
    }
    return out;
}

namespace {

std::string format_by_own_kind(const Value& value, const SheetFormat& format) {
    using core::view::ValueKind;
    switch (value.kind) {
        case ValueKind::Null: return {};
        case ValueKind::Bool: return value.boolean ? format.yes_label : format.no_label;
        case ValueKind::Integer: return std::to_string(value.integer);
        case ValueKind::Number: return format_decimal(value.number, 4, format.decimal_separator);
        case ValueKind::Text: return value.text;
        case ValueKind::Duration: return format_duration_text(value.integer, format);
        case ValueKind::Date: return format_date_text(value.date, format);
    }
    return {};
}

}  // namespace

std::string format_value_text(const Value& value, CellKind kind, const SheetFormat& format) {
    using core::view::ValueKind;
    if (value.is_null()) {
        return {};
    }
    const CellKind resolved = kind == CellKind::Auto ? value_cell_kind(value) : kind;
    switch (resolved) {
        case CellKind::Auto:
            return format_by_own_kind(value, format);
        case CellKind::Text:
            return value.kind == ValueKind::Text ? value.text : format_by_own_kind(value, format);
        case CellKind::Integer:
            switch (value.kind) {
                case ValueKind::Integer:
                case ValueKind::Duration: return std::to_string(value.integer);
                case ValueKind::Number: return format_decimal(value.number, 2, format.decimal_separator);
                case ValueKind::Bool: return value.boolean ? "1" : "0";
                default: return format_by_own_kind(value, format);
            }
        case CellKind::Number:
            switch (value.kind) {
                case ValueKind::Number: return format_decimal(value.number, 4, format.decimal_separator);
                case ValueKind::Integer:
                case ValueKind::Duration: return std::to_string(value.integer);
                default: return format_by_own_kind(value, format);
            }
        case CellKind::Duration:
            switch (value.kind) {
                case ValueKind::Duration:
                case ValueKind::Integer: return format_duration_text(value.integer, format);
                case ValueKind::Number:
                    return format_duration_text(static_cast<std::int64_t>(std::llround(value.number)),
                                                format);
                default: return format_by_own_kind(value, format);
            }
        case CellKind::Date:
            switch (value.kind) {
                case ValueKind::Date: return format_date_text(value.date, format);
                default: return format_by_own_kind(value, format);
            }
        case CellKind::Percent:
            switch (value.kind) {
                case ValueKind::Integer:
                case ValueKind::Duration: return std::to_string(value.integer) + "%";
                case ValueKind::Number: return format_decimal(value.number, 2, format.decimal_separator) + "%";
                default: return format_by_own_kind(value, format);
            }
        case CellKind::Cost:
            switch (value.kind) {
                case ValueKind::Number: return format_grouped_fixed(value.number, format);
                case ValueKind::Integer:
                case ValueKind::Duration:
                    return format_grouped_fixed(static_cast<double>(value.integer), format);
                default: return format_by_own_kind(value, format);
            }
        case CellKind::Bool:
            switch (value.kind) {
                case ValueKind::Bool: return value.boolean ? format.yes_label : format.no_label;
                case ValueKind::Integer:
                case ValueKind::Duration:
                    return value.integer != 0 ? format.yes_label : format.no_label;
                case ValueKind::Number:
                    return value.number != 0.0 ? format.yes_label : format.no_label;
                default: return format_by_own_kind(value, format);
            }
    }
    return {};
}

/* ---- parsing ----------------------------------------------------------- */

EditOutcome parse_edit_text(std::string_view text, CellKind kind, const SheetFormat& format) {
    EditOutcome outcome;
    const std::string trimmed = trim_copy(text);
    if (kind == CellKind::Auto) {
        kind = CellKind::Text;
    }
    if (kind != CellKind::Text && trimmed.empty()) {
        /* Clearing a cell is legal and means "no value". */
        outcome.ok = true;
        return outcome;
    }

    switch (kind) {
        case CellKind::Auto:
            break;
        case CellKind::Text:
            outcome.value = Value::of_text(trimmed);
            outcome.ok = true;
            return outcome;
        case CellKind::Duration: {
            DurationScan scan;
            if (!scan_duration(trimmed, format, scan)) {
                outcome.error = "duration: " + scan.error;
                return outcome;
            }
            const LoweredUnits labels = lower_units(format.units);
            DurationUnit unit = DurationUnit::Day;
            if (!duration_unit_from_text(scan.unit, labels, unit)) {
                outcome.error = "duration: unknown unit " + quote(scan.unit);
                return outcome;
            }
            if (scan.negative && scan.value != 0.0) {
                outcome.error = "duration: a duration cannot be negative";
                return outcome;
            }
            const std::int64_t per_unit = ticks_per_unit(unit, format);
            const double total = scan.value * static_cast<double>(per_unit);
            if (!(total >= -9.0e18 && total <= 9.0e18)) {
                outcome.error = "duration: value is too large";
                return outcome;
            }
            outcome.value = Value::of_duration(static_cast<std::int64_t>(std::llround(total)));
            outcome.ok = true;
            return outcome;
        }
        case CellKind::Date: {
            core::view::DateValue date;
            std::string error;
            if (!parse_date_text(trimmed, format.date, date, error)) {
                outcome.error = "date: " + error;
                return outcome;
            }
            outcome.value = Value::of_date(date);
            outcome.ok = true;
            return outcome;
        }
        case CellKind::Integer: {
            NumberScan scan;
            if (!scan_decimal(trimmed, format, scan)) {
                outcome.error = "integer: " + scan.error;
                return outcome;
            }
            if (!scan.integral) {
                outcome.error = "integer: expected a whole number, got " + quote(trimmed);
                return outcome;
            }
            if (scan.value < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                scan.value > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                outcome.error = "integer: value is too large";
                return outcome;
            }
            outcome.value =
                Value::of_int(static_cast<std::int64_t>(std::llround(scan.value)));
            outcome.ok = true;
            return outcome;
        }
        case CellKind::Number: {
            NumberScan scan;
            if (!scan_decimal(trimmed, format, scan)) {
                outcome.error = "number: " + scan.error;
                return outcome;
            }
            outcome.value = Value::of_number(scan.value);
            outcome.ok = true;
            return outcome;
        }
        case CellKind::Cost: {
            std::string_view digits = trim_view(trimmed);
            if (!format.currency_symbol.empty()) {
                const std::string symbol = format.currency_symbol;
                if (digits.size() >= symbol.size() &&
                    digits.substr(0, symbol.size()) == symbol) {
                    digits = trim_view(digits.substr(symbol.size()));
                }
            }
            NumberScan scan;
            if (!scan_decimal(digits, format, scan)) {
                outcome.error = "cost: " + scan.error;
                return outcome;
            }
            outcome.value = Value::of_number(scan.value);
            outcome.ok = true;
            return outcome;
        }
        case CellKind::Percent: {
            std::string_view digits = trim_view(trimmed);
            if (!digits.empty() && digits.back() == '%') {
                digits = trim_view(digits.substr(0, digits.size() - 1));
            }
            NumberScan scan;
            if (!scan_decimal(digits, format, scan)) {
                outcome.error = "percent: " + scan.error;
                return outcome;
            }
            if (scan.value < 0.0 || scan.value > 100.0) {
                outcome.error = "percent: expected a number between 0 and 100, got " +
                                quote(trimmed);
                return outcome;
            }
            if (scan.integral) {
                outcome.value =
                    Value::of_int(static_cast<std::int64_t>(std::llround(scan.value)));
            } else {
                outcome.value = Value::of_number(scan.value);
            }
            outcome.ok = true;
            return outcome;
        }
        case CellKind::Bool: {
            const std::string lowered = lowercase_ascii(trimmed);
            const std::string yes = lowercase_ascii(format.yes_label);
            const std::string no = lowercase_ascii(format.no_label);
            if (lowered == "yes" || lowered == "y" || lowered == "true" || lowered == "1" ||
                (!yes.empty() && lowered == yes)) {
                outcome.value = Value::of_bool(true);
                outcome.ok = true;
                return outcome;
            }
            if (lowered == "no" || lowered == "n" || lowered == "false" || lowered == "0" ||
                (!no.empty() && lowered == no)) {
                outcome.value = Value::of_bool(false);
                outcome.ok = true;
                return outcome;
            }
            outcome.error = "expected " + format.yes_label + " or " + format.no_label +
                            ", got " + quote(trimmed);
            return outcome;
        }
    }
    outcome.error = "cannot parse a value of that kind";
    return outcome;
}

/* ---- comparisons ------------------------------------------------------- */

bool operator==(const CellRef& a, const CellRef& b) noexcept {
    return a.row == b.row && a.column == b.column;
}

bool operator!=(const CellRef& a, const CellRef& b) noexcept { return !(a == b); }

bool operator==(const CellStyle& a, const CellStyle& b) noexcept {
    return a.align == b.align && a.bold == b.bold && a.red == b.red;
}

bool operator==(const CellRange& a, const CellRange& b) noexcept {
    if (a.valid != b.valid) {
        return false;
    }
    if (!a.valid) {
        return true;
    }
    return a.row_begin == b.row_begin && a.row_end == b.row_end &&
           a.column_begin == b.column_begin && a.column_end == b.column_end;
}

bool operator!=(const CellRange& a, const CellRange& b) noexcept { return !(a == b); }

/* ---- clipboard text (TSV) ---------------------------------------------- */

namespace {

bool field_needs_quotes(std::string_view text) noexcept {
    return text.find_first_of("\t\n\r\"") != std::string_view::npos;
}

std::string quote_field(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char ch : text) {
        if (ch == '"') {
            out.push_back('"');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

}  // namespace

const ClipboardCell* ClipboardBlock::at(std::size_t row_offset,
                                        std::size_t column_offset) const noexcept {
    if (row_offset >= rows || column_offset >= columns) {
        return nullptr;
    }
    for (const ClipboardCell& cell : cells) {
        if (cell.row_offset == row_offset && cell.column_offset == column_offset) {
            return &cell;
        }
    }
    return nullptr;
}

std::string ClipboardBlock::to_text() const {
    std::string out;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            if (column != 0) {
                out.push_back('\t');
            }
            const ClipboardCell* cell = at(row, column);
            const std::string_view text =
                cell != nullptr ? std::string_view(cell->text) : std::string_view{};
            if (field_needs_quotes(text)) {
                out += quote_field(text);
            } else {
                out += text;
            }
        }
        out.push_back('\n');
    }
    return out;
}

ClipboardBlock ClipboardBlock::from_text(std::string_view text) {
    ClipboardBlock block;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool field_started = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const char ch = text[i];
        if (ch == '"' && !field_started && field.empty()) {
            field_started = true;
            ++i;
            while (i < text.size()) {
                if (text[i] == '"') {
                    if (i + 1 < text.size() && text[i + 1] == '"') {
                        field.push_back('"');
                        i += 2;
                        continue;
                    }
                    ++i; /* closing quote */
                    break;
                }
                field.push_back(text[i]);
                ++i;
            }
            /* Anything between the closing quote and the delimiter is ignored;
             * Excel writes nothing there, and dropping it keeps the parser
             * total. */
            while (i < text.size() && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
                ++i;
            }
            continue;
        }
        if (ch == '\t') {
            row.push_back(field);
            field.clear();
            field_started = false;
            ++i;
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            row.push_back(field);
            rows.push_back(row);
            row.clear();
            field.clear();
            field_started = false;
            if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            ++i;
            continue;
        }
        field.push_back(ch);
        field_started = true;
        ++i;
    }
    if (field_started || !field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }

    block.rows = rows.size();
    for (const std::vector<std::string>& source_row : rows) {
        if (source_row.size() > block.columns) {
            block.columns = source_row.size();
        }
    }
    block.cells.reserve(block.rows * block.columns);
    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        for (std::size_t column_index = 0; column_index < rows[row_index].size();
             ++column_index) {
            ClipboardCell cell;
            cell.row_offset = row_index;
            cell.column_offset = column_index;
            cell.kind = CellKind::Text;
            cell.text = rows[row_index][column_index];
            cell.value = Value::of_text(cell.text);
            block.cells.push_back(std::move(cell));
        }
    }
    return block;
}

/* ---- DateFormat presets ------------------------------------------------ */

DateFormat DateFormat::short_day_month_year() {
    DateFormat format;
    format.order = DateOrder::DayMonthYear;
    format.separator = '/';
    format.two_digit_year = true;
    format.show_weekday = true;
    return format;
}

DateFormat DateFormat::short_month_day_year() {
    DateFormat format = short_day_month_year();
    format.order = DateOrder::MonthDayYear;
    return format;
}

DateFormat DateFormat::iso() {
    DateFormat format;
    format.order = DateOrder::YearMonthDay;
    format.separator = '-';
    format.two_digit_year = false;
    format.show_weekday = false;
    return format;
}

/* ---- SheetModel -------------------------------------------------------- */

SheetModel::SheetModel(RecordSet& records, std::vector<SheetColumn> columns)
    : records_(records), columns_(std::move(columns)) {
    rebuild_outline();
    rebuild_column_kinds();
}

SheetModel::SheetModel(RecordSet& records, const core::view::TableDefinition& table)
    : SheetModel(records, columns_from_table(table)) {}

void SheetModel::set_columns(std::vector<SheetColumn> columns) {
    columns_ = std::move(columns);
    rebuild_column_kinds();
    clamp_selection();
    drop_edit_if_hidden();
}

void SheetModel::set_columns(const core::view::TableDefinition& table) {
    set_columns(columns_from_table(table));
}

const SheetColumn& SheetModel::column(std::size_t index) const noexcept {
    static const SheetColumn kEmpty{};
    return index < columns_.size() ? columns_[index] : kEmpty;
}

bool SheetModel::column_visible(std::size_t index) const noexcept {
    return index < columns_.size() && columns_[index].visible;
}

bool SheetModel::set_column_visible(std::size_t index, bool visible) {
    if (index >= columns_.size()) {
        return false;
    }
    columns_[index].visible = visible;
    clamp_selection();
    drop_edit_if_hidden();
    return true;
}

bool SheetModel::set_column_width(std::size_t index, int width) {
    if (index >= columns_.size() || width < 0) {
        return false;
    }
    columns_[index].width = width;
    return true;
}

bool SheetModel::set_column_kind(std::size_t index, CellKind kind) {
    if (index >= columns_.size()) {
        return false;
    }
    columns_[index].kind = kind;
    refresh_column_kind(index);
    return true;
}

bool SheetModel::set_column_title(std::size_t index, std::string title) {
    if (index >= columns_.size()) {
        return false;
    }
    columns_[index].title = std::move(title);
    return true;
}

bool SheetModel::column_read_only(std::size_t index) const noexcept {
    return index < columns_.size() && columns_[index].read_only;
}

bool SheetModel::set_column_read_only(std::size_t index, bool read_only) {
    if (index >= columns_.size()) {
        return false;
    }
    columns_[index].read_only = read_only;
    return true;
}

std::string SheetModel::column_title(std::size_t index) const {
    if (index >= columns_.size()) {
        return {};
    }
    return core::view::field_title(columns_[index].field, records_.record_type(),
                                   columns_[index].title);
}

std::size_t SheetModel::visible_column_count() const noexcept {
    std::size_t count = 0;
    for (const SheetColumn& column : columns_) {
        if (column.visible) {
            ++count;
        }
    }
    return count;
}

std::vector<std::size_t> SheetModel::visible_columns() const {
    std::vector<std::size_t> columns;
    columns.reserve(columns_.size());
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].visible) {
            columns.push_back(i);
        }
    }
    return columns;
}

std::size_t SheetModel::visible_column(std::size_t ordinal) const noexcept {
    if (ordinal == kNoIndex) {
        return kNoIndex;
    }
    std::size_t seen = 0;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (!columns_[i].visible) {
            continue;
        }
        if (seen == ordinal) {
            return i;
        }
        ++seen;
    }
    return kNoIndex;
}

std::size_t SheetModel::display_column(std::size_t index) const noexcept {
    if (!column_visible(index)) {
        return kNoIndex;
    }
    std::size_t seen = 0;
    for (std::size_t i = 0; i < index; ++i) {
        if (columns_[i].visible) {
            ++seen;
        }
    }
    return seen;
}

std::size_t SheetModel::column_of_field(FieldId field) const noexcept {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].field == field) {
            return i;
        }
    }
    return kNoIndex;
}

/* ---- rows and outline -------------------------------------------------- */

std::uint32_t SheetModel::row_uid(std::size_t row) const noexcept {
    return row < records_.row_count() ? records_.uid(row) : 0;
}

int SheetModel::outline_level(std::size_t row) const noexcept {
    if (row >= records_.row_count()) {
        return 0;
    }
    if (row < outline_.size() && outline_[row].has_override) {
        return outline_[row].level;
    }
    return field_outline_level(row);
}

bool SheetModel::has_children(std::size_t row) const noexcept {
    if (row + 1 >= records_.row_count()) {
        return false;
    }
    return outline_level(row + 1) > outline_level(row);
}

bool SheetModel::is_summary(std::size_t row) const noexcept {
    if (row >= records_.row_count()) {
        return false;
    }
    if (row < outline_.size() && outline_[row].has_override) {
        return outline_[row].summary;
    }
    return field_summary(row) || has_children(row);
}

void SheetModel::set_outline_level(std::size_t row, int level, bool summary) {
    if (row >= records_.row_count()) {
        return;
    }
    rebuild_outline();
    outline_[row].has_override = true;
    outline_[row].level = level < 0 ? 0 : level;
    outline_[row].summary = summary;
    if (!is_summary(row) && row < collapsed_.size()) {
        collapsed_[row] = false;
    }
    clamp_selection();
    drop_edit_if_hidden();
}

void SheetModel::clear_outline_level(std::size_t row) {
    if (row >= records_.row_count()) {
        return;
    }
    rebuild_outline();
    outline_[row] = RowOutline{};
    clamp_selection();
    drop_edit_if_hidden();
}

bool SheetModel::is_collapsed(std::size_t row) const noexcept {
    return row < collapsed_.size() && collapsed_[row];
}

bool SheetModel::collapse(std::size_t row) {
    if (row >= records_.row_count() || !is_summary(row)) {
        return false;
    }
    rebuild_outline();
    collapsed_[row] = true;
    clamp_selection();
    drop_edit_if_hidden();
    return true;
}

bool SheetModel::expand(std::size_t row) {
    if (row >= records_.row_count() || !is_summary(row)) {
        return false;
    }
    rebuild_outline();
    collapsed_[row] = false;
    return true;
}

bool SheetModel::toggle_collapsed(std::size_t row) {
    return is_collapsed(row) ? expand(row) : collapse(row);
}

void SheetModel::collapse_all() {
    rebuild_outline();
    for (std::size_t row = 0; row < records_.row_count(); ++row) {
        collapsed_[row] = is_summary(row);
    }
    clamp_selection();
    drop_edit_if_hidden();
}

void SheetModel::expand_all() {
    rebuild_outline();
    std::fill(collapsed_.begin(), collapsed_.end(), false);
}

bool SheetModel::row_visible(std::size_t row) const noexcept {
    if (row >= records_.row_count()) {
        return false;
    }
    const int level = outline_level(row);
    for (std::size_t i = 0; i < row; ++i) {
        if (!is_summary(i) || !is_collapsed(i)) {
            continue;
        }
        const int summary_level = outline_level(i);
        if (summary_level >= level) {
            continue;
        }
        bool descendant = true;
        for (std::size_t j = i + 1; j <= row; ++j) {
            if (outline_level(j) <= summary_level) {
                descendant = false;
                break;
            }
        }
        if (descendant) {
            return false;
        }
    }
    return true;
}

std::vector<std::size_t> SheetModel::visible_rows() const {
    std::vector<std::size_t> rows;
    rows.reserve(records_.row_count());
    std::vector<int> collapsed_levels;
    for (std::size_t row = 0; row < records_.row_count(); ++row) {
        const int level = outline_level(row);
        while (!collapsed_levels.empty() && level <= collapsed_levels.back()) {
            collapsed_levels.pop_back();
        }
        if (!collapsed_levels.empty()) {
            continue;
        }
        rows.push_back(row);
        if (is_summary(row) && is_collapsed(row)) {
            collapsed_levels.push_back(level);
        }
    }
    return rows;
}

std::size_t SheetModel::visible_row_count() const noexcept {
    std::size_t count = 0;
    for (std::size_t row = 0; row < records_.row_count(); ++row) {
        if (row_visible(row)) {
            ++count;
        }
    }
    return count;
}

std::size_t SheetModel::visible_row(std::size_t ordinal) const noexcept {
    if (ordinal == kNoIndex) {
        return kNoIndex;
    }
    std::size_t seen = 0;
    std::vector<int> collapsed_levels;
    for (std::size_t row = 0; row < records_.row_count(); ++row) {
        const int level = outline_level(row);
        while (!collapsed_levels.empty() && level <= collapsed_levels.back()) {
            collapsed_levels.pop_back();
        }
        if (!collapsed_levels.empty()) {
            continue;
        }
        if (seen == ordinal) {
            return row;
        }
        ++seen;
        if (is_summary(row) && is_collapsed(row)) {
            collapsed_levels.push_back(level);
        }
    }
    return kNoIndex;
}

std::size_t SheetModel::display_row(std::size_t row) const noexcept {
    if (!row_visible(row)) {
        return kNoIndex;
    }
    std::size_t seen = 0;
    for (std::size_t i = 0; i < row; ++i) {
        if (row_visible(i)) {
            ++seen;
        }
    }
    return seen;
}

/* ---- per-row height ---------------------------------------------------- */

bool SheetModel::set_row_height(std::size_t row, int height) {
    if (row >= records_.row_count() || height < 0) {
        return false;
    }
    rebuild_outline();
    row_heights_[row] = height;
    return true;
}

int SheetModel::row_height_override(std::size_t row) const noexcept {
    return row < row_heights_.size() ? row_heights_[row] : 0;
}

int SheetModel::row_height_points(std::size_t row, const SheetMetrics& metrics) const noexcept {
    const int override = row_height_override(row);
    return override > 0 ? override : metrics.row_height;
}

/* ---- cells ------------------------------------------------------------- */

const Value* SheetModel::cell_value(std::size_t row, std::size_t index) const noexcept {
    if (row >= records_.row_count() || index >= columns_.size()) {
        return nullptr;
    }
    return records_.get(row, columns_[index].field);
}

CellKind SheetModel::cell_kind(std::size_t row, std::size_t index) const noexcept {
    if (index >= columns_.size()) {
        return CellKind::Text;
    }
    if (columns_[index].kind != CellKind::Auto) {
        return columns_[index].kind;
    }
    if (const Value* value = cell_value(row, index); value != nullptr && !value->is_null()) {
        return value_cell_kind(*value);
    }
    return index < column_kinds_.size() ? column_kinds_[index] : CellKind::Text;
}

std::string SheetModel::cell_text(std::size_t row, std::size_t index) const {
    if (row >= records_.row_count() || index >= columns_.size()) {
        return {};
    }
    const Value* value = cell_value(row, index);
    if (value == nullptr || value->is_null()) {
        return {};
    }
    return format_value_text(*value, cell_kind(row, index), format_);
}

CellStyle SheetModel::cell_style(std::size_t row, std::size_t index) const {
    CellStyle style;
    const SheetColumn& source = column(index);
    switch (source.align) {
        case core::view::ColumnAlign::Left: style.align = CellAlign::Left; break;
        case core::view::ColumnAlign::Center: style.align = CellAlign::Center; break;
        case core::view::ColumnAlign::Right: style.align = CellAlign::Right; break;
        case core::view::ColumnAlign::Auto:
        default:
            style.align = is_numeric_kind(cell_kind(row, index)) ? CellAlign::Right
                                                                 : CellAlign::Left;
            break;
    }
    style.bold = is_summary(row);
    style.red = is_critical(row) || is_overdue(row);
    return style;
}

std::string SheetModel::format_value_as(const Value& value, CellKind kind) const {
    return format_value_text(value, kind, format_);
}

std::string SheetModel::field_text(std::size_t row, FieldId field) const {
    if (row >= records_.row_count() || field == 0) {
        return {};
    }
    const Value* value = records_.get(row, field);
    if (value == nullptr || value->is_null()) {
        return {};
    }
    return format_value_text(*value, value_cell_kind(*value), format_);
}

bool SheetModel::is_critical(std::size_t row) const noexcept {
    return critical_field_ != 0 && field_flag(row, critical_field_);
}

bool SheetModel::is_overdue(std::size_t row) const noexcept {
    if (!has_status_date_ || finish_field_ == 0) {
        return false;
    }
    const Value* finish = records_.get(row, finish_field_);
    if (finish == nullptr || finish->kind != core::view::ValueKind::Date ||
        !finish->date.valid()) {
        return false;
    }
    double percent = 0.0;
    if (percent_field_ != 0) {
        if (const Value* value = records_.get(row, percent_field_); value != nullptr) {
            if (value->kind == core::view::ValueKind::Integer ||
                value->kind == core::view::ValueKind::Duration) {
                percent = static_cast<double>(value->integer);
            } else if (value->kind == core::view::ValueKind::Number) {
                percent = value->number;
            }
        }
    }
    if (percent >= 100.0) {
        return false;
    }
    return finish->date < status_date_;
}

void SheetModel::set_status_date(const core::view::DateValue& date) {
    status_date_ = date;
    has_status_date_ = date.valid();
}

void SheetModel::clear_status_date() noexcept {
    has_status_date_ = false;
    status_date_ = core::view::DateValue{};
}

/* ---- selection --------------------------------------------------------- */

bool SheetModel::select_cell(std::size_t row, std::size_t index) {
    if (row >= records_.row_count() || index >= columns_.size() || !row_visible(row) ||
        !column_visible(index)) {
        return false;
    }
    anchor_ = CellRef{row, index};
    focus_ = anchor_;
    has_selection_ = true;
    return true;
}

bool SheetModel::extend_to(std::size_t row, std::size_t index) {
    if (row >= records_.row_count() || index >= columns_.size()) {
        return false;
    }
    if (!has_selection_) {
        return select_cell(row, index);
    }
    const std::size_t clamped_row = clamp_row(row);
    const std::size_t clamped_column = clamp_column(index);
    if (clamped_row == kNoIndex || clamped_column == kNoIndex) {
        return false;
    }
    focus_ = CellRef{clamped_row, clamped_column};
    return true;
}

void SheetModel::set_anchor(std::size_t row, std::size_t index) {
    if (row >= records_.row_count() || index >= columns_.size() || !row_visible(row) ||
        !column_visible(index)) {
        return;
    }
    anchor_ = CellRef{row, index};
    if (!has_selection_) {
        focus_ = anchor_;
        has_selection_ = true;
    }
}

void SheetModel::select_all() {
    const std::size_t rows = visible_row_count();
    const std::size_t columns = visible_column_count();
    if (rows == 0 || columns == 0) {
        clear_selection();
        return;
    }
    const std::size_t last_row = visible_row(rows - 1);
    const std::size_t last_column = visible_column(columns - 1);
    if (last_row == kNoIndex || last_column == kNoIndex) {
        clear_selection();
        return;
    }
    anchor_ = CellRef{visible_row(0), visible_column(0)};
    focus_ = CellRef{last_row, last_column};
    has_selection_ = true;
}

void SheetModel::clear_selection() noexcept {
    has_selection_ = false;
    anchor_ = CellRef{};
    focus_ = CellRef{};
}

bool SheetModel::is_selected(std::size_t row, std::size_t index) const noexcept {
    if (!has_selection_ || !row_visible(row) || !column_visible(index)) {
        return false;
    }
    const std::size_t row_begin = std::min(anchor_.row, focus_.row);
    const std::size_t row_end = std::max(anchor_.row, focus_.row);
    const std::size_t column_begin = std::min(anchor_.column, focus_.column);
    const std::size_t column_end = std::max(anchor_.column, focus_.column);
    return row >= row_begin && row <= row_end && index >= column_begin && index <= column_end;
}

std::vector<std::size_t> SheetModel::selected_rows() const {
    std::vector<std::size_t> rows;
    if (!has_selection_) {
        return rows;
    }
    const std::size_t row_begin = std::min(anchor_.row, focus_.row);
    const std::size_t row_end = std::max(anchor_.row, focus_.row);
    const std::size_t column_begin = std::min(anchor_.column, focus_.column);
    const std::size_t column_end = std::max(anchor_.column, focus_.column);
    if (column_begin > column_end) {
        return rows;
    }
    for (std::size_t row : visible_rows()) {
        if (row >= row_begin && row <= row_end) {
            rows.push_back(row);
        }
    }
    return rows;
}

CellRange SheetModel::selection_range() const noexcept {
    CellRange range;
    if (!has_selection_) {
        return range;
    }
    range.row_begin = std::min(anchor_.row, focus_.row);
    range.row_end = std::max(anchor_.row, focus_.row);
    range.column_begin = std::min(anchor_.column, focus_.column);
    range.column_end = std::max(anchor_.column, focus_.column);
    range.valid = true;
    return range;
}

bool SheetModel::select_range(std::size_t row_begin, std::size_t row_end,
                              std::size_t column_begin, std::size_t column_end) {
    if (visible_row_count() == 0 || visible_column_count() == 0) {
        return false;
    }
    const std::size_t first_row = clamp_row(std::min(row_begin, row_end));
    const std::size_t last_row = clamp_row(std::max(row_begin, row_end));
    const std::size_t first_column = clamp_column(std::min(column_begin, column_end));
    const std::size_t last_column = clamp_column(std::max(column_begin, column_end));
    if (first_row == kNoIndex || last_row == kNoIndex || first_column == kNoIndex ||
        last_column == kNoIndex) {
        return false;
    }
    anchor_ = CellRef{first_row, first_column};
    focus_ = CellRef{last_row, last_column};
    has_selection_ = true;
    return true;
}

std::vector<CellRef> SheetModel::selected_cells() const {
    std::vector<CellRef> cells;
    const CellRange range = selection_range();
    if (!range.valid) {
        return cells;
    }
    for (std::size_t row : visible_rows()) {
        if (row < range.row_begin || row > range.row_end) {
            continue;
        }
        for (std::size_t column : visible_columns()) {
            if (column >= range.column_begin && column <= range.column_end) {
                cells.push_back(CellRef{row, column});
            }
        }
    }
    return cells;
}

/* ---- editing ----------------------------------------------------------- */

namespace {

/*
 * Whether an already-typed value may be written to a cell of `kind` without
 * going back through the text parser.  Null fits every kind (it clears the
 * cell); the numeric kinds accept the wider numeric storage kinds so a Number
 * can target a Cost/Percent/Number cell.  A Duration never silently becomes an
 * Integer (the units would be ambiguous), and Auto is never a resolved target.
 */
bool value_fits_kind(const Value& value, CellKind kind) noexcept {
    using core::view::ValueKind;
    if (value.is_null() || kind == CellKind::Auto) {
        return true;
    }
    switch (kind) {
        case CellKind::Text: return value.kind == ValueKind::Text;
        case CellKind::Bool: return value.kind == ValueKind::Bool;
        case CellKind::Date: return value.kind == ValueKind::Date;
        case CellKind::Duration: return value.kind == ValueKind::Duration;
        case CellKind::Integer: return value.kind == ValueKind::Integer;
        case CellKind::Number:
        case CellKind::Cost:
        case CellKind::Percent:
            return value.kind == ValueKind::Integer || value.kind == ValueKind::Number;
        case CellKind::Auto:
        default: return true;
    }
}

std::string row_column_text(std::size_t row, std::size_t column) {
    return "row " + std::to_string(row + 1) + ", column " + std::to_string(column + 1);
}

}  // namespace

bool SheetModel::begin_edit(std::size_t row, std::size_t index) {
    if (row >= records_.row_count() || index >= columns_.size() || !row_visible(row) ||
        !column_visible(index) || column_read_only(index)) {
        return false;
    }
    editing_ = true;
    edit_target_ = CellRef{row, index};
    edit_text_ = cell_text(row, index);
    return true;
}

EditOutcome SheetModel::validate_edit() const {
    EditOutcome outcome;
    if (!editing_ || edit_target_.row >= records_.row_count() ||
        edit_target_.column >= columns_.size()) {
        outcome.error = "no edit in progress";
        return outcome;
    }
    return parse_edit_text(edit_text_, cell_kind(edit_target_.row, edit_target_.column), format_);
}

EditOutcome SheetModel::parse_text(std::string_view text, CellKind kind) const {
    return parse_edit_text(text, kind, format_);
}

EditOutcome SheetModel::commit_edit() { return commit_edit(false); }

EditOutcome SheetModel::commit_edit(bool coalesce_with_previous) {
    EditOutcome outcome;
    if (!editing_ || edit_target_.row >= records_.row_count() ||
        edit_target_.column >= columns_.size()) {
        outcome.error = "no edit in progress";
        return outcome;
    }
    const std::size_t row = edit_target_.row;
    const std::size_t index = edit_target_.column;
    if (columns_[index].read_only) {
        outcome.error = "column is read-only";
        return outcome;
    }
    const FieldId field = columns_[index].field;
    const Value* current = cell_value(row, index);
    const Value before = current != nullptr ? *current : Value{};

    outcome = parse_edit_text(edit_text_, cell_kind(row, index), format_);
    if (!outcome.ok) {
        /* The session stays open: the UI shows outcome.error and the text the
         * user typed is unchanged. */
        return outcome;
    }

    records_.set_field(row, field, outcome.value);
    outcome.changed = !values_equal(before, outcome.value);
    if (outcome.changed) {
        bool merged = false;
        if (coalesce_with_previous && !undo_.empty()) {
            UndoStep& top = undo_.back();
            if ((top.kind == UndoKind::Cell || top.kind == UndoKind::Typing) &&
                !top.entries.empty()) {
                UndoEntry& last = top.entries.back();
                if (last.row == row && last.field == field) {
                    last.after = outcome.value;
                    top.kind = UndoKind::Typing;
                    if (values_equal(last.before, last.after)) {
                        /* The typing run came back to where it started: the
                         * whole step is a no-op, so drop it. */
                        top.entries.pop_back();
                        if (top.entries.empty()) {
                            undo_.pop_back();
                        }
                    }
                    redo_.clear();
                    merged = true;
                }
            }
        }
        if (!merged) {
            UndoStep step;
            step.kind = UndoKind::Cell;
            step.label = "Edit " + core::view::field_title(field, records_.record_type());
            UndoEntry entry;
            entry.row = row;
            entry.field = field;
            entry.before = before;
            entry.after = outcome.value;
            step.entries.push_back(std::move(entry));
            push_undo_step(std::move(step));
        }
    }
    refresh_field_columns(field);

    editing_ = false;
    edit_target_ = CellRef{};
    edit_text_.clear();
    return outcome;
}

void SheetModel::cancel_edit() noexcept {
    editing_ = false;
    edit_target_ = CellRef{};
    edit_text_.clear();
}

/* ---- transactions ------------------------------------------------------ */

bool SheetModel::field_read_only(FieldId field) const noexcept {
    bool found = false;
    for (const SheetColumn& column : columns_) {
        if (column.field == field) {
            found = true;
            if (!column.read_only) {
                return false;
            }
        }
    }
    return found;
}

bool SheetModel::resolve_column_command(std::size_t row, std::size_t column,
                                        EditCommand& command, std::string& error) const {
    if (row >= records_.row_count() || column >= columns_.size()) {
        error = "edit target is out of range";
        return false;
    }
    if (!row_visible(row) || !column_visible(column)) {
        error = "edit target is not a visible cell";
        return false;
    }
    if (columns_[column].read_only) {
        error = "column is read-only";
        return false;
    }
    if (columns_[column].field == 0) {
        error = "column has no field";
        return false;
    }
    command = EditCommand{};
    command.row = row;
    command.column = column;
    command.field = columns_[column].field;
    command.kind = cell_kind(row, column);
    return true;
}

bool SheetModel::resolve_field_command(std::size_t row, FieldId field, CellKind kind,
                                       EditCommand& command, std::string& error) const {
    if (row >= records_.row_count()) {
        error = "edit target is out of range";
        return false;
    }
    if (field == 0) {
        error = "edit has no field";
        return false;
    }
    std::size_t column = kNoIndex;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].field != field) {
            continue;
        }
        if (column == kNoIndex) {
            column = i; /* first column for the field, hidden or not */
        }
        if (columns_[i].visible) {
            column = i;
            break;
        }
    }
    if (field_read_only(field)) {
        error = "column is read-only";
        return false;
    }
    command = EditCommand{};
    command.row = row;
    command.column = column;
    command.field = field;
    command.kind = kind;
    if (command.kind == CellKind::Auto) {
        command.kind = column != kNoIndex ? cell_kind(row, column) : field_cell_kind(field);
    }
    return true;
}

bool SheetModel::begin_transaction(UndoKind kind, std::string label) {
    if (transaction_active_ || editing_) {
        return false;
    }
    staged_.clear();
    transaction_active_ = true;
    transaction_kind_ = kind;
    transaction_label_ = std::move(label);
    return true;
}

const EditCommand* SheetModel::staged(std::size_t index) const noexcept {
    return index < staged_.size() ? &staged_[index] : nullptr;
}

EditOutcome SheetModel::validate_staged(std::size_t index) const {
    EditOutcome outcome;
    if (index >= staged_.size()) {
        outcome.error = "no staged edit at that index";
        return outcome;
    }
    std::string error;
    Value value;
    if (!validate_command(staged_[index], value, error)) {
        outcome.error = std::move(error);
        return outcome;
    }
    outcome.ok = true;
    outcome.value = std::move(value);
    return outcome;
}

bool SheetModel::stage_edit(std::size_t row, std::size_t column, std::string text) {
    if (!transaction_active_) {
        return false;
    }
    EditCommand command;
    std::string error;
    if (!resolve_column_command(row, column, command, error)) {
        return false;
    }
    command.text = std::move(text);
    staged_.push_back(std::move(command));
    return true;
}

bool SheetModel::stage_field_edit(std::size_t row, FieldId field, std::string text,
                                  CellKind kind) {
    if (!transaction_active_) {
        return false;
    }
    EditCommand command;
    std::string error;
    if (!resolve_field_command(row, field, kind, command, error)) {
        return false;
    }
    command.text = std::move(text);
    staged_.push_back(std::move(command));
    return true;
}

bool SheetModel::stage_value(std::size_t row, std::size_t column, Value value) {
    if (!transaction_active_) {
        return false;
    }
    EditCommand command;
    std::string error;
    if (!resolve_column_command(row, column, command, error)) {
        return false;
    }
    command.is_value = true;
    command.value = std::move(value);
    staged_.push_back(std::move(command));
    return true;
}

bool SheetModel::stage_field_value(std::size_t row, FieldId field, Value value, CellKind kind) {
    if (!transaction_active_) {
        return false;
    }
    EditCommand command;
    std::string error;
    if (!resolve_field_command(row, field, kind, command, error)) {
        return false;
    }
    command.is_value = true;
    command.value = std::move(value);
    staged_.push_back(std::move(command));
    return true;
}

bool SheetModel::push_command(EditCommand command) {
    if (!transaction_active_) {
        return false;
    }
    if (command.row >= records_.row_count()) {
        return false;
    }
    if (command.field == 0 && command.column != kNoIndex &&
        command.column < columns_.size()) {
        command.field = columns_[command.column].field;
    }
    if (command.field == 0) {
        return false;
    }
    if (command.kind == CellKind::Auto) {
        if (command.column != kNoIndex && command.column < columns_.size() &&
            columns_[command.column].field == command.field) {
            command.kind = cell_kind(command.row, command.column);
        } else {
            command.kind = field_cell_kind(command.field);
        }
    }
    const bool read_only =
        command.column != kNoIndex && command.column < columns_.size() &&
                columns_[command.column].field == command.field
            ? columns_[command.column].read_only
            : field_read_only(command.field);
    if (read_only) {
        return false;
    }
    staged_.push_back(std::move(command));
    return true;
}

bool SheetModel::set_staged_text(std::size_t index, std::string text) {
    if (index >= staged_.size()) {
        return false;
    }
    staged_[index].is_value = false;
    staged_[index].value = Value{};
    staged_[index].text = std::move(text);
    return true;
}

bool SheetModel::set_staged_value(std::size_t index, Value value) {
    if (index >= staged_.size()) {
        return false;
    }
    staged_[index].is_value = true;
    staged_[index].text.clear();
    staged_[index].value = std::move(value);
    return true;
}

bool SheetModel::discard_staged(std::size_t index) {
    if (index >= staged_.size()) {
        return false;
    }
    staged_.erase(staged_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool SheetModel::validate_command(const EditCommand& command, Value& value,
                                  std::string& error) const {
    if (command.row >= records_.row_count()) {
        error = "edit target is out of range";
        return false;
    }
    if (command.field == 0) {
        error = "edit has no field";
        return false;
    }
    if (command.kind == CellKind::Auto) {
        error = "edit has no resolved kind";
        return false;
    }
    const bool read_only =
        command.column != kNoIndex && command.column < columns_.size() &&
                columns_[command.column].field == command.field
            ? columns_[command.column].read_only
            : field_read_only(command.field);
    if (read_only) {
        error = row_column_text(command.row,
                                command.column != kNoIndex ? command.column : 0) +
                ": column is read-only";
        return false;
    }
    if (command.is_value) {
        if (!value_fits_kind(command.value, command.kind)) {
            error = row_column_text(command.row,
                                    command.column != kNoIndex ? command.column : 0) +
                    ": the typed value does not fit the cell's kind";
            return false;
        }
        value = command.value;
        return true;
    }
    EditOutcome parsed = parse_edit_text(command.text, command.kind, format_);
    if (!parsed.ok) {
        error = row_column_text(command.row,
                                command.column != kNoIndex ? command.column : 0) +
                ": " + parsed.error;
        return false;
    }
    value = std::move(parsed.value);
    return true;
}

TransactionResult SheetModel::apply_commands(UndoKind kind, const std::string& label) {
    TransactionResult result;
    result.staged = staged_.size();
    std::vector<Value> values(staged_.size());
    for (std::size_t i = 0; i < staged_.size(); ++i) {
        std::string error;
        if (!validate_command(staged_[i], values[i], error)) {
            result.failed_index = i;
            result.failed_cell = CellRef{staged_[i].row, staged_[i].column};
            result.error = std::move(error);
            return result;
        }
    }
    for (std::size_t i = 0; i < staged_.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (staged_[i].row == staged_[j].row && staged_[i].field == staged_[j].field) {
                result.failed_index = i;
                result.failed_cell = CellRef{staged_[i].row, staged_[i].column};
                result.error = row_column_text(staged_[i].row,
                                               staged_[i].column != kNoIndex
                                                   ? staged_[i].column
                                                   : 0) +
                               ": duplicate edit for the same cell";
                return result;
            }
        }
    }

    UndoStep step;
    step.kind = kind;
    step.label = label;
    for (std::size_t i = 0; i < staged_.size(); ++i) {
        const EditCommand& command = staged_[i];
        const Value* current = records_.get(command.row, command.field);
        const Value before = current != nullptr ? *current : Value{};
        if (values_equal(before, values[i])) {
            continue;
        }
        records_.set_field(command.row, command.field, values[i]);
        UndoEntry entry;
        entry.row = command.row;
        entry.field = command.field;
        entry.before = before;
        entry.after = values[i];
        step.entries.push_back(std::move(entry));
        ++result.changed;
    }
    for (const EditCommand& command : staged_) {
        refresh_field_columns(command.field);
    }
    if (!step.entries.empty()) {
        if (step.label.empty()) {
            step.label = kind == UndoKind::Paste ? "Paste" : "Multiple edits";
        }
        push_undo_step(std::move(step));
    }
    result.ok = true;
    return result;
}

TransactionResult SheetModel::apply_transaction() {
    TransactionResult result;
    if (!transaction_active_) {
        result.error = "no transaction in progress";
        return result;
    }
    result = apply_commands(transaction_kind_, transaction_label_);
    if (result.ok) {
        clear_transaction_state();
    }
    return result;
}

void SheetModel::cancel_transaction() noexcept { clear_transaction_state(); }

void SheetModel::clear_transaction_state() noexcept {
    transaction_active_ = false;
    transaction_kind_ = UndoKind::Transaction;
    transaction_label_.clear();
    staged_.clear();
}

/* ---- clipboard --------------------------------------------------------- */

ClipboardBlock SheetModel::copy_selection() const {
    ClipboardBlock block;
    const CellRange range = selection_range();
    if (!range.valid) {
        return block;
    }
    std::vector<std::size_t> rows;
    for (std::size_t row : visible_rows()) {
        if (row >= range.row_begin && row <= range.row_end) {
            rows.push_back(row);
        }
    }
    std::vector<std::size_t> columns;
    for (std::size_t column : visible_columns()) {
        if (column >= range.column_begin && column <= range.column_end) {
            columns.push_back(column);
        }
    }
    if (rows.empty() || columns.empty()) {
        return block;
    }
    block.rows = rows.size();
    block.columns = columns.size();
    block.cells.reserve(block.rows * block.columns);
    for (std::size_t row_offset = 0; row_offset < rows.size(); ++row_offset) {
        for (std::size_t column_offset = 0; column_offset < columns.size(); ++column_offset) {
            const std::size_t row = rows[row_offset];
            const std::size_t column = columns[column_offset];
            ClipboardCell cell;
            cell.row_offset = row_offset;
            cell.column_offset = column_offset;
            cell.field = columns_[column].field;
            cell.kind = cell_kind(row, column);
            if (const Value* value = cell_value(row, column); value != nullptr) {
                cell.value = *value;
            }
            cell.text = cell_text(row, column);
            block.cells.push_back(std::move(cell));
        }
    }
    return block;
}

PasteResult SheetModel::paste(const ClipboardBlock& block, std::size_t row, std::size_t column,
                              PasteMode mode) {
    PasteResult result;
    if (transaction_active_ || editing_) {
        result.error = "cannot paste while an edit or transaction is open";
        return result;
    }
    if (block.empty()) {
        result.ok = true;
        return result;
    }
    const std::size_t expected = block.rows * block.columns;
    if (expected / block.rows != block.columns) {
        result.error = "clipboard block is too large";
        return result;
    }
    if (row >= records_.row_count() || column >= columns_.size() || !row_visible(row) ||
        !column_visible(column)) {
        result.error = "paste target is not a visible cell";
        return result;
    }
    const std::size_t base_row_ordinal = display_row(row);
    const std::size_t base_column_ordinal = display_column(column);
    const std::vector<std::size_t> visible_column_indices = visible_columns();

    std::vector<EditCommand> commands;
    commands.reserve(block.cells.size());
    std::size_t skipped = 0;
    std::size_t considered = 0;
    std::size_t first_row = kNoIndex;
    std::size_t last_row = 0;
    std::size_t first_column = kNoIndex;
    std::size_t last_column = 0;
    for (std::size_t i = 0; i < block.cells.size(); ++i) {
        const ClipboardCell& source = block.cells[i];
        if (source.row_offset >= block.rows || source.column_offset >= block.columns) {
            ++skipped;
            continue;
        }
        ++considered;
        const std::size_t target_row = visible_row(base_row_ordinal + source.row_offset);
        if (target_row == kNoIndex) {
            ++skipped;
            continue;
        }
        std::size_t target_column = kNoIndex;
        if (source.field != 0) {
            for (std::size_t candidate : visible_column_indices) {
                if (columns_[candidate].field == source.field) {
                    target_column = candidate;
                    break;
                }
            }
        }
        if (target_column == kNoIndex) {
            target_column = visible_column(base_column_ordinal + source.column_offset);
        }
        if (target_column == kNoIndex) {
            ++skipped;
            continue;
        }
        if (columns_[target_column].read_only) {
            result.failed_index = i;
            result.failed_cell = CellRef{target_row, target_column};
            result.error = row_column_text(target_row, target_column) + ": column is read-only";
            return result;
        }
        EditCommand command;
        command.row = target_row;
        command.column = target_column;
        command.field = columns_[target_column].field;
        command.kind = cell_kind(target_row, target_column);
        if (source.value.is_null()) {
            command.is_value = true; /* an empty source cell clears the target */
        } else {
            const CellKind source_kind =
                source.kind != CellKind::Auto ? source.kind : value_cell_kind(source.value);
            if (mode == PasteMode::Values && source_kind == command.kind &&
                value_fits_kind(source.value, command.kind)) {
                command.is_value = true;
                command.value = source.value;
            } else if (mode == PasteMode::Values) {
                command.text = format_value_text(source.value, source_kind, format_);
            } else {
                command.text = source.text;
                if (command.text.empty()) {
                    command.text = format_value_text(source.value, source_kind, format_);
                }
            }
        }
        first_row = std::min(first_row, target_row);
        last_row = std::max(last_row, target_row);
        first_column = std::min(first_column, target_column);
        last_column = std::max(last_column, target_column);
        commands.push_back(std::move(command));
    }
    if (considered < expected) {
        skipped += expected - considered;
    }

    const std::size_t command_count = commands.size();
    const std::string label =
        command_count == 1 ? "Paste" : "Paste " + std::to_string(command_count) + " cells";
    staged_ = std::move(commands);
    const TransactionResult transaction = apply_commands(UndoKind::Paste, label);
    staged_.clear();
    if (!transaction.ok) {
        result.failed_index = transaction.failed_index;
        result.failed_cell = transaction.failed_cell;
        result.error = transaction.error;
        return result;
    }
    result.ok = true;
    result.cells = transaction.staged;
    result.changed = transaction.changed;
    result.skipped = skipped;
    if (command_count != 0) {
        select_range(first_row, last_row, first_column, last_column);
    }
    return result;
}

PasteResult SheetModel::paste(const ClipboardBlock& block, PasteMode mode) {
    PasteResult result;
    if (!has_selection_) {
        result.error = "no selection to paste into";
        return result;
    }
    const CellRange range = selection_range();
    return paste(block, range.row_begin, range.column_begin, mode);
}

/* ---- undo / redo ------------------------------------------------------- */

const UndoEntry* SheetModel::next_undo() const noexcept {
    if (undo_.empty() || undo_.back().entries.empty()) {
        return nullptr;
    }
    return &undo_.back().entries.back();
}

const UndoEntry* SheetModel::next_redo() const noexcept {
    if (redo_.empty() || redo_.back().entries.empty()) {
        return nullptr;
    }
    return &redo_.back().entries.back();
}

const UndoStep* SheetModel::next_undo_step() const noexcept {
    return undo_.empty() ? nullptr : &undo_.back();
}

const UndoStep* SheetModel::next_redo_step() const noexcept {
    return redo_.empty() ? nullptr : &redo_.back();
}

std::size_t SheetModel::undo_entry_count() const noexcept {
    std::size_t count = 0;
    for (const UndoStep& step : undo_) {
        count += step.entries.size();
    }
    return count;
}

std::size_t SheetModel::redo_entry_count() const noexcept {
    std::size_t count = 0;
    for (const UndoStep& step : redo_) {
        count += step.entries.size();
    }
    return count;
}

std::string SheetModel::step_label(const UndoStep& step) const {
    if (!step.label.empty()) {
        return step.label;
    }
    if (!step.entries.empty()) {
        return "Edit " +
               core::view::field_title(step.entries.back().field, records_.record_type());
    }
    switch (step.kind) {
        case UndoKind::Paste: return "Paste";
        case UndoKind::Transaction: return "Multiple edits";
        case UndoKind::Typing:
        case UndoKind::Cell:
        default: return "Edit";
    }
}

std::string SheetModel::undo_label() const {
    const UndoStep* step = next_undo_step();
    return step != nullptr ? step_label(*step) : std::string{};
}

std::string SheetModel::redo_label() const {
    const UndoStep* step = next_redo_step();
    return step != nullptr ? step_label(*step) : std::string{};
}

bool SheetModel::push_undo_step(UndoStep step) {
    if (step.entries.empty()) {
        return false;
    }
    undo_.push_back(std::move(step));
    redo_.clear();
    return true;
}

void SheetModel::apply_step(const UndoStep& step, bool forward) {
    if (forward) {
        for (const UndoEntry& entry : step.entries) {
            records_.set_field(entry.row, entry.field, entry.after);
            refresh_field_columns(entry.field);
        }
    } else {
        for (auto it = step.entries.rbegin(); it != step.entries.rend(); ++it) {
            records_.set_field(it->row, it->field, it->before);
            refresh_field_columns(it->field);
        }
    }
}

bool SheetModel::undo() { return undo(1); }

bool SheetModel::undo(std::size_t steps) {
    if (steps == 0) {
        return false;
    }
    bool applied = false;
    while (steps != 0 && !undo_.empty()) {
        UndoStep step = std::move(undo_.back());
        undo_.pop_back();
        apply_step(step, false);
        redo_.push_back(std::move(step));
        --steps;
        applied = true;
    }
    return applied;
}

bool SheetModel::redo() { return redo(1); }

bool SheetModel::redo(std::size_t steps) {
    if (steps == 0) {
        return false;
    }
    bool applied = false;
    while (steps != 0 && !redo_.empty()) {
        UndoStep step = std::move(redo_.back());
        redo_.pop_back();
        apply_step(step, true);
        undo_.push_back(std::move(step));
        --steps;
        applied = true;
    }
    return applied;
}

void SheetModel::clear_history() noexcept {
    undo_.clear();
    redo_.clear();
}

/* ---- geometry ---------------------------------------------------------- */

int SheetModel::column_width_points(std::size_t index,
                                    const SheetMetrics& metrics) const noexcept {
    if (!column_visible(index)) {
        return 0;
    }
    const int width = columns_[index].width;
    if (width <= 0) {
        return 0; /* auto: the shell measures the header and cells */
    }
    int points = width * metrics.char_width;
    if (points > 0 && points < metrics.min_column_width) {
        points = metrics.min_column_width;
    }
    return points;
}

std::vector<ColumnSegment> SheetModel::layout_columns(const SheetMetrics& metrics,
                                                      int auto_width) const {
    std::vector<ColumnSegment> segments;
    segments.reserve(visible_column_count());
    int x = 0;
    for (std::size_t ordinal = 0;; ++ordinal) {
        const std::size_t index = visible_column(ordinal);
        if (index == kNoIndex) {
            break;
        }
        int width = column_width_points(index, metrics);
        if (width <= 0) {
            width = auto_width > 0 ? auto_width : 0;
        }
        ColumnSegment segment;
        segment.column = index;
        segment.x = x;
        segment.width = width;
        segments.push_back(segment);
        x += width;
    }
    return segments;
}

int SheetModel::sheet_width_points(const SheetMetrics& metrics, int auto_width) const {
    const std::vector<ColumnSegment> segments = layout_columns(metrics, auto_width);
    if (segments.empty()) {
        return 0;
    }
    const ColumnSegment& last = segments.back();
    return last.x + last.width;
}

int SheetModel::row_top_points(std::size_t display_ordinal,
                               const SheetMetrics& metrics) const {
    if (display_ordinal == kNoIndex) {
        return -1;
    }
    int y = metrics.header_height;
    for (std::size_t ordinal = 0; ordinal < display_ordinal; ++ordinal) {
        const std::size_t row = visible_row(ordinal);
        if (row == kNoIndex) {
            return -1;
        }
        y += row_height_points(row, metrics);
    }
    if (visible_row(display_ordinal) == kNoIndex) {
        return -1;
    }
    return y;
}

int SheetModel::content_height_points(const SheetMetrics& metrics) const {
    int height = metrics.header_height;
    for (std::size_t row : visible_rows()) {
        height += row_height_points(row, metrics);
    }
    return height;
}

int SheetModel::indent_points(std::size_t row, const SheetMetrics& metrics) const noexcept {
    const int level = outline_level(row);
    if (level <= 0) {
        return 0;
    }
    return level * metrics.indent_per_level;
}

/* ---- internals --------------------------------------------------------- */

void SheetModel::rebuild_outline() {
    const std::size_t rows = records_.row_count();
    if (outline_.size() < rows) {
        outline_.resize(rows);
    }
    if (collapsed_.size() < rows) {
        collapsed_.resize(rows, false);
    }
    if (row_heights_.size() < rows) {
        row_heights_.resize(rows, 0);
    }

    outline_level_field_ = 0;
    summary_field_ = 0;
    critical_field_ = 0;
    finish_field_ = 0;
    percent_field_ = 0;
    const core::model::RecordType type = records_.record_type();
    auto resolve = [type](const char* name) -> FieldId {
        FieldId field = 0;
        core::view::field_id_by_name(type, name, field);
        return field;
    };
    outline_level_field_ = resolve("OutlineLevel");
    summary_field_ = resolve("Summary");
    critical_field_ = resolve("Critical");
    finish_field_ = resolve("Finish");
    percent_field_ = resolve("PercentComplete");
}

void SheetModel::rebuild_column_kinds() {
    column_kinds_.assign(columns_.size(), CellKind::Text);
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        refresh_column_kind(i);
    }
}

void SheetModel::refresh_column_kind(std::size_t index) {
    if (index >= columns_.size()) {
        return;
    }
    if (column_kinds_.size() != columns_.size()) {
        column_kinds_.resize(columns_.size(), CellKind::Text);
    }
    if (columns_[index].kind != CellKind::Auto) {
        column_kinds_[index] = columns_[index].kind;
        return;
    }
    for (std::size_t row = 0; row < records_.row_count(); ++row) {
        if (const Value* value = cell_value(row, index);
            value != nullptr && !value->is_null()) {
            column_kinds_[index] = value_cell_kind(*value);
            return;
        }
    }
    /* An empty column falls back to the descriptor table's storage kind.  Note
     * that Task.Duration is stored as i32 ticks, so an *empty* duration column
     * with no explicit CellKind resolves to Integer: declare the kind on the
     * column when the table itself cannot show the intent. */
    switch (core::view::field_value_kind(columns_[index].field)) {
        case core::view::ValueKind::Text: column_kinds_[index] = CellKind::Text; break;
        case core::view::ValueKind::Bool: column_kinds_[index] = CellKind::Bool; break;
        case core::view::ValueKind::Integer: column_kinds_[index] = CellKind::Integer; break;
        case core::view::ValueKind::Number: column_kinds_[index] = CellKind::Number; break;
        case core::view::ValueKind::Duration: column_kinds_[index] = CellKind::Duration; break;
        case core::view::ValueKind::Date: column_kinds_[index] = CellKind::Date; break;
        case core::view::ValueKind::Null:
        default: column_kinds_[index] = CellKind::Text; break;
    }
}

void SheetModel::refresh_field_columns(FieldId field) {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].field == field) {
            refresh_column_kind(i);
        }
    }
}

void SheetModel::clamp_selection() {
    if (!has_selection_) {
        return;
    }
    const std::size_t row_anchor = clamp_row(anchor_.row);
    const std::size_t row_focus = clamp_row(focus_.row);
    const std::size_t column_anchor = clamp_column(anchor_.column);
    const std::size_t column_focus = clamp_column(focus_.column);
    if (row_anchor == kNoIndex || row_focus == kNoIndex || column_anchor == kNoIndex ||
        column_focus == kNoIndex) {
        clear_selection();
        return;
    }
    anchor_ = CellRef{row_anchor, column_anchor};
    focus_ = CellRef{row_focus, column_focus};
}

std::size_t SheetModel::clamp_row(std::size_t row) const {
    if (row < records_.row_count() && row_visible(row)) {
        return row;
    }
    std::size_t best = kNoIndex;
    for (std::size_t candidate : visible_rows()) {
        if (candidate <= row && (best == kNoIndex || candidate > best)) {
            best = candidate;
        }
    }
    if (best != kNoIndex) {
        return best;
    }
    const std::size_t first = visible_row(0);
    return first; /* kNoIndex when nothing is visible */
}

std::size_t SheetModel::clamp_column(std::size_t index) const {
    if (index < columns_.size() && columns_[index].visible) {
        return index;
    }
    std::size_t best = kNoIndex;
    for (std::size_t candidate = 0; candidate < columns_.size(); ++candidate) {
        if (columns_[candidate].visible && candidate <= index) {
            best = candidate;
        }
    }
    if (best != kNoIndex) {
        return best;
    }
    return visible_column(0);
}

void SheetModel::drop_edit_if_hidden() noexcept {
    if (!editing_) {
        return;
    }
    if (edit_target_.row >= records_.row_count() || edit_target_.column >= columns_.size() ||
        !row_visible(edit_target_.row) || !column_visible(edit_target_.column)) {
        cancel_edit();
    }
}

int SheetModel::field_outline_level(std::size_t row) const noexcept {
    if (outline_level_field_ == 0) {
        return 0;
    }
    const Value* value = records_.get(row, outline_level_field_);
    if (value == nullptr) {
        return 0;
    }
    int level = 0;
    switch (value->kind) {
        case core::view::ValueKind::Integer:
        case core::view::ValueKind::Duration: level = static_cast<int>(value->integer); break;
        case core::view::ValueKind::Number: level = static_cast<int>(value->number); break;
        case core::view::ValueKind::Bool: level = value->boolean ? 1 : 0; break;
        default: return 0;
    }
    // -1 is the project-summary row (wave 23): same text indent as a top-level
    // task but the parent of every level-0 task.  Anything below that is not a
    // level this model produces and clamps back to -1.
    return level < -1 ? -1 : level;
}

bool SheetModel::field_summary(std::size_t row) const noexcept {
    return summary_field_ != 0 && field_flag(row, summary_field_);
}

bool SheetModel::field_flag(std::size_t row, FieldId field) const noexcept {
    if (field == 0) {
        return false;
    }
    const Value* value = records_.get(row, field);
    if (value == nullptr) {
        return false;
    }
    switch (value->kind) {
        case core::view::ValueKind::Bool: return value->boolean;
        case core::view::ValueKind::Integer:
        case core::view::ValueKind::Duration: return value->integer != 0;
        case core::view::ValueKind::Number: return value->number != 0.0;
        default: return false;
    }
}

void SheetModel::refresh() {
    rebuild_outline();
    rebuild_column_kinds();
    clamp_selection();
    drop_edit_if_hidden();
}

}  // namespace core::sheet
