/*
 * value.h -- the value type every view surface reads.
 *
 * The field model is keyed by PjField id (core/model/field_table.h).  A field's
 * *storage* kind is one of seven things; this header is the portable carrier
 * for those seven, plus the date/duration encodings the MPP reader and MSPDI
 * both need.
 *
 * ENCODINGS
 *   Duration  int64 ticks of 6 seconds.  4800 ticks = one 8-hour day, which is
 *             the same unit as the reader's kTicksPerDay (spec/mpp-records.md
 *             section 4) and lets MSPDI "PT8H0M0S" convert without loss.
 *   Date      a civil (timezone-naive) y/m/d h:m:s.  MSPDI dateTimes and the
 *             reader's packed date have no timezone in the file; converting to
 *             UTC would invent information.  The calendar engine owns any
 *             timezone policy.
 *   Text      UTF-8 std::string.  Empty text is a real value; Value::is_null()
 *             is how "no value" is spelled.
 *
 * NO EXCEPTIONS, NO ALLOCATION except Text longer than the small-string buffer,
 * which is why a RecordSet column stores Value by value rather than by pointer.
 */

#ifndef CORE_VIEW_VALUE_H
#define CORE_VIEW_VALUE_H

#include <cstdint>
#include <string>
#include <string_view>

namespace core::view {

using FieldId = std::uint32_t;

/* One 6-second tick, the MPP row unit.  These mirror mpp::kTicksPerDay without
 * including core/mpp (the view layer must build without the reader). */
inline constexpr std::int64_t kTicksPerSecond = 1;
inline constexpr std::int64_t kTicksPerMinute = 10;
inline constexpr std::int64_t kTicksPerHour = 600;
inline constexpr std::int64_t kTicksPerDay = 4800;

struct DateValue {
    std::int32_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;

    bool valid() const noexcept;
    bool has_time_of_day() const noexcept { return hour != 0 || minute != 0 || second != 0; }
};

bool operator==(const DateValue& a, const DateValue& b) noexcept;
bool operator<(const DateValue& a, const DateValue& b) noexcept;

enum class ValueKind : std::uint8_t {
    Null = 0,
    Bool,
    Integer,
    Number,
    Text,
    Duration,
    Date,
};

const char* value_kind_name(ValueKind kind) noexcept;
bool value_kind_from_name(std::string_view name, ValueKind& out) noexcept;

/*
 * A field value.  Only the member matching `kind` is meaningful; the rest are
 * left at their defaults so that copying a Value is trivially cheap for all
 * non-Text values (the std::string is empty and does not allocate).
 */
struct Value {
    ValueKind kind = ValueKind::Null;
    bool boolean = false;
    std::int64_t integer = 0;   /* Integer and Duration (ticks) */
    double number = 0.0;        /* Number */
    DateValue date{};           /* Date */
    std::string text;           /* Text */

    static Value null() noexcept { return Value{}; }
    static Value of_bool(bool v) noexcept;
    static Value of_int(std::int64_t v) noexcept;
    static Value of_number(double v) noexcept;
    static Value of_text(std::string v);
    static Value of_duration(std::int64_t ticks) noexcept;
    static Value of_date(DateValue v) noexcept;

    bool is_null() const noexcept { return kind == ValueKind::Null; }
};

/*
 * MSPDI / ISO-8601 duration ("PT16H0M0S", "P2D", "-PT1H30M").  Returns false
 * for calendar-duration forms with years or months (ambiguous) and for
 * malformed input.  Ticks are rounded to the nearest 6 seconds.
 */
bool parse_iso_duration(std::string_view text, std::int64_t& ticks) noexcept;

/*
 * "YYYY-MM-DD", "YYYY-MM-DDTHH:MM", or with ":SS" and an optional fractional
 * part.  A trailing "Z" or +/-HH:MM offset is accepted and ignored: the file's
 * civil time is preserved as-is (see the note at the top).
 */
bool parse_iso_datetime(std::string_view text, DateValue& out) noexcept;

enum class DurationStyle : std::uint8_t {
    Iso8601 = 0, /* "PT16H0M0S" -- lossless, the CSV default */
    Days,        /* "2d", "1.5d" -- the sheet's working-day rendering */
    Ticks,       /* "9600" */
};

/* "YYYY-MM-DD" or "YYYY-MM-DD HH:MM[:SS]"; "" for an invalid date. */
std::string format_date(const DateValue& date);

/* Renders a Duration to the chosen style.  Non-duration values return "". */
std::string format_duration(std::int64_t ticks,
                            DurationStyle style = DurationStyle::Iso8601);

/*
 * A display/interchange rendering of any value: text unchanged, booleans "0"/"1"
 * (MSPDI-compatible), dates and durations per the arguments, numbers with up to
 * 15 significant digits and no trailing zeros, null "".
 */
std::string format_value(const Value& value,
                         DurationStyle duration_style = DurationStyle::Iso8601);

/*
 * Ordering used by SORT and GROUP.  Values of different kinds are coerced to a
 * common kind first (Text "8" vs Integer 8 compares numerically); if no
 * coercion is possible the kinds are ordered Null < Bool < Integer < Number <
 * Duration < Date < Text, so the result is always a total order.  Callers decide
 * where Null records go; compare() still defines an order for them.
 */
int compare_values(const Value& a, const Value& b) noexcept;

/*
 * Best-effort conversion for filter constants and comparisons.  Text to
 * Integer/Number/Duration/Date/Bool parses the text; Integer to Number widens;
 * Number to Integer truncates only when exact; everything else returns Null.
 */
Value coerce_value(const Value& value, ValueKind target) noexcept;

}  // namespace core::view

#endif  // CORE_VIEW_VALUE_H
