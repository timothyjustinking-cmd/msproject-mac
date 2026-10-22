#include "eval.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fields.h"

namespace core::formula {
namespace {

Value eval_node(const Expr& expr, const EvalContext& context);

constexpr std::int64_t kTicksPerDay = core::view::kTicksPerDay;
constexpr std::int64_t kTicksPerMinute = core::view::kTicksPerMinute;

struct Numeric {
    bool ok = false;
    double value = 0.0;
    bool integer = true;
};

Numeric numeric_of(const Value& value) {
    Numeric out;
    switch (value.kind) {
        case core::view::ValueKind::Null:
            return out;
        case core::view::ValueKind::Bool:
            out.ok = true;
            out.value = value.boolean ? 1.0 : 0.0;
            out.integer = true;
            return out;
        case core::view::ValueKind::Integer:
            out.ok = true;
            out.value = static_cast<double>(value.integer);
            out.integer = true;
            return out;
        case core::view::ValueKind::Number:
            out.ok = true;
            out.value = value.number;
            out.integer = false;
            return out;
        case core::view::ValueKind::Duration:
            out.ok = true;
            out.value = static_cast<double>(value.integer);
            out.integer = true;
            return out;
        case core::view::ValueKind::Text: {
            const Value parsed = core::view::coerce_value(value, core::view::ValueKind::Number);
            if (parsed.is_null()) {
                return out;
            }
            out.ok = true;
            out.value = parsed.number;
            out.integer = false;
            return out;
        }
        case core::view::ValueKind::Date:
            return out;
    }
    return out;
}

bool round_to_ticks(double value, std::int64_t& out) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }
    constexpr double kLimit = 9.2e18;
    if (value <= -kLimit || value >= kLimit) {
        return false;
    }
    out = static_cast<std::int64_t>(std::llround(value));
    return true;
}

/*
 * A numeric operand as duration ticks.  A Duration is its tick count; any
 * other number is DAYS (4800 ticks), which is the inference documented in
 * eval.h.  Text is parsed as a number first.
 */
bool duration_ticks_of(const Value& value, std::int64_t& out) {
    switch (value.kind) {
        case core::view::ValueKind::Duration:
            out = value.integer;
            return true;
        case core::view::ValueKind::Integer:
        case core::view::ValueKind::Number:
        case core::view::ValueKind::Bool:
        case core::view::ValueKind::Text: {
            const Numeric numeric = numeric_of(value);
            if (!numeric.ok) {
                return false;
            }
            return round_to_ticks(numeric.value * static_cast<double>(kTicksPerDay), out);
        }
        case core::view::ValueKind::Null:
        case core::view::ValueKind::Date:
            return false;
    }
    return false;
}

bool trunc_days(double value, std::int64_t& out) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }
    constexpr double kLimit = 9.2e18;
    if (value <= -kLimit || value >= kLimit) {
        return false;
    }
    out = static_cast<std::int64_t>(value);  /* toward zero, as documented */
    return true;
}

Value value_from_double(double value, bool integer) {
    if (!std::isfinite(value)) {
        return Value::null();
    }
    if (integer && std::fabs(value) <= 9007199254740992.0) {
        std::int64_t rounded = 0;
        if (round_to_ticks(value, rounded)) {
            return Value::of_int(rounded);
        }
    }
    return Value::of_number(value);
}

/*
 * Dates are WALL-CLOCK civil values; Duration ticks are the working-time unit
 * (4800 ticks = one nominal day, 600 ticks = one hour).  Date arithmetic
 * therefore goes through this documented bridge (inference; Project itself
 * would consult the task calendar, which this module does not have):
 *
 *   ticks -> date shift   whole kTicksPerDay blocks become calendar days; the
 *                         leftover ticks become wall-clock seconds at 6 s/tick.
 *                         So +1d (4800) is the next day same time, +4h (2400)
 *                         is four hours later, and the two are consistent.
 *   date - date           whole calendar days apart x 4800 + the time-of-day
 *                         difference in ticks.  It is the exact inverse of the
 *                         shift above, so (d + x) - d == x.
 *
 * Using 6 s/tick for a whole day would make 08:00 look like one day.
 */
constexpr std::int64_t kSecondsPerDay = 86400;
constexpr std::int64_t kSecondsPerTick = 6;

bool date_to_seconds(const core::view::DateValue& date, std::int64_t& out) noexcept {
    if (!date.valid()) {
        return false;
    }
    const std::int64_t days =
        static_cast<std::int64_t>(days_from_civil(date.year, date.month, date.day)) -
        kPackedDateEpochDay;
    const std::int64_t seconds =
        static_cast<std::int64_t>(date.hour) * 3600 + static_cast<std::int64_t>(date.minute) * 60 +
        date.second;
    out = days * kSecondsPerDay + seconds;
    return true;
}

core::view::DateValue date_from_seconds(std::int64_t total) noexcept {
    std::int64_t days = total / kSecondsPerDay;
    std::int64_t remainder = total % kSecondsPerDay;
    if (remainder < 0) {
        remainder += kSecondsPerDay;
        days -= 1;
    }
    core::view::DateValue out =
        civil_from_days(static_cast<std::int32_t>(days + kPackedDateEpochDay));
    out.hour = static_cast<std::uint8_t>(remainder / 3600);
    out.minute = static_cast<std::uint8_t>((remainder % 3600) / 60);
    out.second = static_cast<std::uint8_t>(remainder % 60);
    return out;
}

/* The wall-clock seconds a Duration shifts a date by (see the comment above). */
std::int64_t duration_wall_seconds(std::int64_t ticks) noexcept {
    std::int64_t days = ticks / kTicksPerDay;
    std::int64_t remainder = ticks % kTicksPerDay;
    if (remainder < 0) {
        remainder += kTicksPerDay;
        days -= 1;
    }
    return days * kSecondsPerDay + remainder * kSecondsPerTick;
}

/* date_left - date_right as Duration ticks; the inverse of the shift above. */
Value date_difference(const core::view::DateValue& left, const core::view::DateValue& right) {
    if (!left.valid() || !right.valid()) {
        return Value::null();
    }
    const std::int64_t left_days = days_from_civil(left.year, left.month, left.day);
    const std::int64_t right_days = days_from_civil(right.year, right.month, right.day);
    const std::int64_t left_seconds = static_cast<std::int64_t>(left.hour) * 3600 +
                                      static_cast<std::int64_t>(left.minute) * 60 + left.second;
    const std::int64_t right_seconds = static_cast<std::int64_t>(right.hour) * 3600 +
                                       static_cast<std::int64_t>(right.minute) * 60 + right.second;
    std::int64_t remainder_ticks = 0;
    if (!round_to_ticks(
            static_cast<double>(left_seconds - right_seconds) / static_cast<double>(kSecondsPerTick),
            remainder_ticks)) {
        return Value::null();
    }
    return Value::of_duration((left_days - right_days) * kTicksPerDay + remainder_ticks);
}

bool date_of(const Value& value, core::view::DateValue& out) {
    if (value.kind == core::view::ValueKind::Date) {
        out = value.date;
        return out.valid();
    }
    if (value.kind == core::view::ValueKind::Text) {
        return core::view::parse_iso_datetime(value.text, out) && out.valid();
    }
    return false;
}

Value arithmetic(BinaryOp op, const Value& a, const Value& b) {
    if (a.is_null() || b.is_null()) {
        return Value::null();
    }
    const bool a_date = a.kind == core::view::ValueKind::Date;
    const bool b_date = b.kind == core::view::ValueKind::Date;
    const bool a_duration = a.kind == core::view::ValueKind::Duration;
    const bool b_duration = b.kind == core::view::ValueKind::Duration;

    if (op == BinaryOp::Add || op == BinaryOp::Subtract) {
        if (a_date && b_date) {
            if (op == BinaryOp::Subtract) {
                return date_difference(a.date, b.date);
            }
            return Value::null();
        }
        if (a_date && b_duration) {
            std::int64_t start = 0;
            if (!date_to_seconds(a.date, start)) {
                return Value::null();
            }
            const std::int64_t delta = op == BinaryOp::Add
                                           ? duration_wall_seconds(b.integer)
                                           : -duration_wall_seconds(b.integer);
            return Value::of_date(date_from_seconds(start + delta));
        }
        if (a_date) {
            const Numeric operand = numeric_of(b);
            std::int64_t start = 0;
            std::int64_t days = 0;
            if (!operand.ok || !date_to_seconds(a.date, start) || !trunc_days(operand.value, days)) {
                return Value::null();
            }
            const std::int64_t delta = (op == BinaryOp::Add ? days : -days) * kSecondsPerDay;
            return Value::of_date(date_from_seconds(start + delta));
        }
        if (b_date) {
            if (op != BinaryOp::Add || a_duration) {
                return Value::null();
            }
            const Numeric operand = numeric_of(a);
            std::int64_t start = 0;
            std::int64_t days = 0;
            if (!operand.ok || !date_to_seconds(b.date, start) || !trunc_days(operand.value, days)) {
                return Value::null();
            }
            return Value::of_date(date_from_seconds(start + days * kSecondsPerDay));
        }
        if (a_duration || b_duration) {
            std::int64_t left = 0;
            std::int64_t right = 0;
            if (!duration_ticks_of(a, left) || !duration_ticks_of(b, right)) {
                return Value::null();
            }
            return Value::of_duration(op == BinaryOp::Add ? left + right : left - right);
        }
        const Numeric left = numeric_of(a);
        const Numeric right = numeric_of(b);
        if (!left.ok || !right.ok) {
            return Value::null();
        }
        const bool integer = left.integer && right.integer;
        const double result =
            op == BinaryOp::Add ? left.value + right.value : left.value - right.value;
        return value_from_double(result, integer);
    }

    if (op == BinaryOp::Multiply) {
        if (a_date || b_date) {
            return Value::null();
        }
        if (a_duration || b_duration) {
            const Value& duration = a_duration ? a : b;
            const Value& other = a_duration ? b : a;
            if (other.kind == core::view::ValueKind::Duration) {
                return Value::null();
            }
            const Numeric factor = numeric_of(other);
            std::int64_t ticks = 0;
            if (!factor.ok ||
                !round_to_ticks(static_cast<double>(duration.integer) * factor.value, ticks)) {
                return Value::null();
            }
            return Value::of_duration(ticks);
        }
        const Numeric left = numeric_of(a);
        const Numeric right = numeric_of(b);
        if (!left.ok || !right.ok) {
            return Value::null();
        }
        return value_from_double(left.value * right.value, left.integer && right.integer);
    }

    /* Divide. */
    if (a_date || b_date) {
        return Value::null();
    }
    if (a_duration && b_duration) {
        if (b.integer == 0) {
            return Value::null();
        }
        return Value::of_number(static_cast<double>(a.integer) /
                                static_cast<double>(b.integer));
    }
    if (a_duration) {
        const Numeric divisor = numeric_of(b);
        std::int64_t ticks = 0;
        if (!divisor.ok || divisor.value == 0.0 ||
            !round_to_ticks(static_cast<double>(a.integer) / divisor.value, ticks)) {
            return Value::null();
        }
        return Value::of_duration(ticks);
    }
    if (b_duration) {
        return Value::null();
    }
    const Numeric left = numeric_of(a);
    const Numeric right = numeric_of(b);
    if (!left.ok || !right.ok || right.value == 0.0) {
        return Value::null();
    }
    return Value::of_number(left.value / right.value);
}

Value comparison(BinaryOp op, const Value& a, const Value& b) {
    if (a.is_null() || b.is_null()) {
        return Value::null();
    }
    const int order = core::view::compare_values(a, b);
    switch (op) {
        case BinaryOp::Equal: return Value::of_bool(order == 0);
        case BinaryOp::NotEqual: return Value::of_bool(order != 0);
        case BinaryOp::Less: return Value::of_bool(order < 0);
        case BinaryOp::LessEqual: return Value::of_bool(order <= 0);
        case BinaryOp::Greater: return Value::of_bool(order > 0);
        case BinaryOp::GreaterEqual: return Value::of_bool(order >= 0);
        default: return Value::null();
    }
}

Value unary(UnaryOp op, const Value& value) {
    if (op == UnaryOp::Not) {
        return Value::of_bool(!truthy(value));
    }
    if (op == UnaryOp::Plus) {
        const Numeric numeric = numeric_of(value);
        if (!numeric.ok) {
            return Value::null();
        }
        return numeric.integer ? value_from_double(numeric.value, true)
                               : Value::of_number(numeric.value);
    }
    if (value.is_null()) {
        return Value::null();
    }
    if (value.kind == core::view::ValueKind::Duration) {
        return Value::of_duration(-value.integer);
    }
    const Numeric numeric = numeric_of(value);
    if (!numeric.ok) {
        return Value::null();
    }
    return value_from_double(-numeric.value, numeric.integer);
}

std::string lower_ascii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

Value call_function(const Expr& expr, const EvalContext& context) {
    const std::string& name = expr.text;
    if (static_cast<int>(expr.children.size()) != function_arity(name)) {
        return Value::null();
    }
    std::vector<Value> args;
    args.reserve(expr.children.size());
    for (const Expr& child : expr.children) {
        args.push_back(eval_node(child, context));
    }
    auto number_of = [](double value) { return Value::of_number(value); };
    if (name == "isnull") {
        return Value::of_bool(args[0].is_null());
    }
    if (name == "projdurvalue") {
        std::int64_t ticks = 0;
        if (!duration_ticks_of(args[0], ticks)) {
            return Value::null();
        }
        return number_of(static_cast<double>(ticks) / static_cast<double>(kTicksPerMinute));
    }
    if (name == "projdurconv") {
        std::int64_t ticks = 0;
        if (!duration_ticks_of(args[0], ticks)) {
            return Value::null();
        }
        const std::string unit = lower_ascii(to_text(args[1], context.format));
        double divisor = 0.0;
        if (unit == "d" || unit == "day" || unit == "days") {
            divisor = static_cast<double>(kTicksPerDay);
        } else if (unit == "h" || unit == "hr" || unit == "hrs" || unit == "hour" ||
                   unit == "hours") {
            divisor = 600.0;
        } else if (unit == "m" || unit == "min" || unit == "mins" || unit == "minute" ||
                   unit == "minutes") {
            divisor = static_cast<double>(kTicksPerMinute);
        } else if (unit == "w" || unit == "wk" || unit == "wks" || unit == "week" ||
                   unit == "weeks") {
            divisor = 24000.0;
        } else if (unit == "mo" || unit == "mon" || unit == "mons" || unit == "month" ||
                   unit == "months") {
            divisor = 96000.0;
        } else if (unit == "s" || unit == "sec" || unit == "secs" || unit == "second" ||
                   unit == "seconds") {
            return number_of(static_cast<double>(ticks) * 6.0);
        } else {
            return Value::null();
        }
        return number_of(static_cast<double>(ticks) / divisor);
    }
    if (name == "projdateadd" || name == "projdatesub") {
        core::view::DateValue date;
        std::int64_t ticks = 0;
        if (!date_of(args[0], date) || !duration_ticks_of(args[1], ticks)) {
            return Value::null();
        }
        std::int64_t start = 0;
        if (!date_to_seconds(date, start)) {
            return Value::null();
        }
        const std::int64_t delta = name == "projdateadd" ? duration_wall_seconds(ticks)
                                                         : -duration_wall_seconds(ticks);
        return Value::of_date(date_from_seconds(start + delta));
    }
    if (name == "projdatediff") {
        core::view::DateValue left;
        core::view::DateValue right;
        if (!date_of(args[0], left) || !date_of(args[1], right)) {
            return Value::null();
        }
        return date_difference(left, right);
    }
    if (name == "datevalue") {
        core::view::DateValue date;
        if (args[0].kind == core::view::ValueKind::Text) {
            if (!core::view::parse_iso_datetime(args[0].text, date)) {
                return Value::null();
            }
        } else if (!date_of(args[0], date)) {
            return Value::null();
        }
        return Value::of_date(date);
    }
    if (name == "day" || name == "month" || name == "year" || name == "hour" ||
        name == "minute" || name == "weekday") {
        core::view::DateValue date;
        if (!date_of(args[0], date)) {
            return Value::null();
        }
        if (name == "day") return Value::of_int(date.day);
        if (name == "month") return Value::of_int(date.month);
        if (name == "year") return Value::of_int(date.year);
        if (name == "hour") return Value::of_int(date.hour);
        if (name == "minute") return Value::of_int(date.minute);
        const std::int64_t days = days_from_civil(date.year, date.month, date.day);
        const int raw = static_cast<int>(days % 7);
        const int weekday = ((raw + 4) % 7 + 7) % 7 + 1;
        return Value::of_int(weekday);
    }
    return Value::null();
}

}  // namespace

Value base_value_of_field(const RecordSet& records, std::size_t row, FieldId field) noexcept {
    if (row >= records.row_count()) {
        return Value::null();
    }
    if (const Value* value = records.get(row, field); value != nullptr && !value->is_null()) {
        return *value;
    }
    if (field == kTaskDurationTextField) {
        if (const Value* duration = records.get(row, kTaskDurationField);
            duration != nullptr && !duration->is_null()) {
            return *duration;
        }
    }
    return Value::null();
}

bool truthy(const Value& value) noexcept {
    switch (value.kind) {
        case core::view::ValueKind::Null: return false;
        case core::view::ValueKind::Bool: return value.boolean;
        case core::view::ValueKind::Integer:
        case core::view::ValueKind::Duration: return value.integer != 0;
        case core::view::ValueKind::Number: return value.number != 0.0;
        case core::view::ValueKind::Date: return value.date.valid();
        case core::view::ValueKind::Text: {
            if (value.text.empty()) {
                return false;
            }
            const std::string lower = lower_ascii(value.text);
            return !(lower == "0" || lower == "no" || lower == "false");
        }
    }
    return false;
}

std::string to_text(const Value& value, const core::sheet::SheetFormat& format) {
    return core::sheet::format_value_text(value, core::sheet::CellKind::Auto, format);
}

namespace {

Value eval_node(const Expr& expr, const EvalContext& context) {
    switch (expr.kind) {
        case ExprKind::Number:
            return Value::of_number(expr.number);
        case ExprKind::Text:
            return Value::of_text(expr.text);
        case ExprKind::Date:
            return Value::of_date(expr.date);
        case ExprKind::Duration:
            return Value::of_duration(expr.ticks);
        case ExprKind::Bool:
            return Value::of_bool(expr.boolean);
        case ExprKind::FieldRef:
            /* A resolve callback is authoritative: the computed-column layer
             * answers for every field, computed or base. */
            if (context.resolve) {
                return context.resolve(expr.field);
            }
            if (context.records != nullptr) {
                return base_value_of_field(*context.records, context.row, expr.field);
            }
            return Value::null();
        case ExprKind::Unary:
            return unary(expr.unary, eval_node(expr.children[0], context));
        case ExprKind::Binary: {
            if (expr.binary == BinaryOp::And) {
                const Value left = eval_node(expr.children[0], context);
                if (!truthy(left)) {
                    return Value::of_bool(false);
                }
                return Value::of_bool(truthy(eval_node(expr.children[1], context)));
            }
            if (expr.binary == BinaryOp::Or) {
                const Value left = eval_node(expr.children[0], context);
                if (truthy(left)) {
                    return Value::of_bool(true);
                }
                return Value::of_bool(truthy(eval_node(expr.children[1], context)));
            }
            const Value left = eval_node(expr.children[0], context);
            const Value right = eval_node(expr.children[1], context);
            if (expr.binary == BinaryOp::Concat) {
                return Value::of_text(to_text(left, context.format) + to_text(right, context.format));
            }
            if (expr.binary == BinaryOp::Equal || expr.binary == BinaryOp::NotEqual ||
                expr.binary == BinaryOp::Less || expr.binary == BinaryOp::LessEqual ||
                expr.binary == BinaryOp::Greater || expr.binary == BinaryOp::GreaterEqual) {
                return comparison(expr.binary, left, right);
            }
            return arithmetic(expr.binary, left, right);
        }
        case ExprKind::Call:
            if (expr.text == "iif") {
                const Value condition = eval_node(expr.children[0], context);
                return eval_node(truthy(condition) ? expr.children[1] : expr.children[2], context);
            }
            return call_function(expr, context);
    }
    return Value::null();
}

}  // namespace

namespace {

/*
 * A structurally malformed AST can only come from a hand-built Expr (the
 * parser rejects it), but the evaluator indexes children[0]/[1] and must not
 * read past the end.  A formula that parses is always well formed.
 */
bool check_structure(const Expr& expr, std::string& error) {
    switch (expr.kind) {
        case ExprKind::Unary:
            if (expr.children.size() != 1) {
                error = "malformed formula: unary operator without one operand";
                return false;
            }
            break;
        case ExprKind::Binary:
            if (expr.children.size() != 2) {
                error = "malformed formula: binary operator without two operands";
                return false;
            }
            break;
        case ExprKind::Call: {
            const int arity = function_arity(expr.text);
            if (arity < 0) {
                error = "unknown function \"" + expr.text + "\"";
                return false;
            }
            if (expr.children.size() != static_cast<std::size_t>(arity)) {
                error = "function " + expr.text + " takes " + std::to_string(arity) +
                        " argument(s), got " + std::to_string(expr.children.size());
                return false;
            }
            break;
        }
        case ExprKind::FieldRef:
            if (expr.field == 0) {
                error = "malformed formula: field reference without a field id";
                return false;
            }
            break;
        case ExprKind::Number:
        case ExprKind::Text:
        case ExprKind::Date:
        case ExprKind::Duration:
        case ExprKind::Bool:
            return true;
    }
    for (const Expr& child : expr.children) {
        if (!check_structure(child, error)) {
            return false;
        }
    }
    return true;
}

}  // namespace

EvalResult evaluate(const Expr& expr, const EvalContext& context) {
    EvalResult result;
    if (!check_structure(expr, result.error)) {
        result.ok = false;
        return result;
    }
    result.value = eval_node(expr, context);
    result.ok = true;
    return result;
}

}  // namespace core::formula
