// tests/formula_tests.cpp -- core/formula: tokenizer, parser and its error
// positions, the evaluator (arithmetic/comparison/concat/IIf/date/duration),
// the measured custom-field storage encodings, the custom-field registry and
// computed columns.
//
// Every expectation carries a tag in its label:
//   [measured]   backed by docs/analysis/oracle/w10-oracle.md section 2 (the
//                custom-field ids, the VarMeta block encodings, the [MSPJ<id>]
//                tokens) or by the generated descriptor table.
//   [inference]  our semantic choice; the oracle never evaluated a formula.
//
// The fixtures are built in this file -- no corpus, no MS Project.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "columns.h"
#include "eval.h"
#include "expr.h"
#include "fields.h"
#include "field_table.h"
#include "record_set.h"
#include "sheet.h"
#include "value.h"

namespace {

using core::formula::BinaryOp;
using core::formula::ComputedColumn;
using core::formula::ComputedColumns;
using core::formula::CustomField;
using core::formula::DecodedValue;
using core::formula::EncodedValue;
using core::formula::EvalContext;
using core::formula::FieldKind;
using core::formula::StorageConfidence;
using core::formula::TokenKind;
using core::model::RecordType;
using core::view::DateValue;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;
using core::view::ValueKind;

namespace detail {

template <typename T>
std::string show(const T& value) {
    if constexpr (std::is_integral_v<T> && sizeof(T) < sizeof(int)) {
        return std::to_string(static_cast<int>(value));
    } else if constexpr (std::is_integral_v<T>) {
        return std::to_string(value);
    } else if constexpr (requires(const T& v, std::ostream& out) { out << v; }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<value>";
    }
}

}  // namespace detail

class TestContext {
  public:
    void check(bool condition, const std::string& what) {
        if (condition) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << '\n';
    }

    template <typename A, typename B>
    void equal(const A& actual, const B& expected, const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << detail::show(actual)
                  << ", expected=" << detail::show(expected) << ")\n";
    }

    void contains(const std::string& haystack, const std::string& needle,
                  const std::string& what) {
        if (haystack.find(needle) != std::string::npos) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (missing \"" << needle << "\" in \"" << haystack
                  << "\")\n";
    }

    int finish(const std::string& name) const {
        if (failures_ == 0) {
            std::cout << name << ": " << checks_ << " checks passed\n";
            return 0;
        }
        std::cerr << name << ": " << failures_ << " of " << (checks_ + failures_)
                  << " checks FAILED\n";
        return 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

FieldId task_field(const char* name) {
    const core::model::FieldInfo* info =
        core::model::find_field_by_name(name, RecordType::Task);
    return info == nullptr ? 0 : info->field_id;
}

DateValue date_of(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
    DateValue date;
    date.year = year;
    date.month = static_cast<std::uint8_t>(month);
    date.day = static_cast<std::uint8_t>(day);
    date.hour = static_cast<std::uint8_t>(hour);
    date.minute = static_cast<std::uint8_t>(minute);
    date.second = static_cast<std::uint8_t>(second);
    return date;
}

Value eval_at(const RecordSet& records, std::size_t row, std::string_view formula) {
    const core::formula::ParseResult parsed = core::formula::parse(formula);
    if (!parsed.ok) {
        return Value::null();
    }
    EvalContext context;
    context.records = &records;
    context.row = row;
    const core::formula::EvalResult result = core::formula::evaluate(parsed.root, context);
    return result.value;
}

bool is_number(const Value& value, double expected) {
    return value.kind == ValueKind::Number && std::fabs(value.number - expected) < 1e-9;
}

bool is_integer(const Value& value, std::int64_t expected) {
    return value.kind == ValueKind::Integer && value.integer == expected;
}

bool is_duration(const Value& value, std::int64_t expected) {
    return value.kind == ValueKind::Duration && value.integer == expected;
}

bool is_bool(const Value& value, bool expected) {
    return value.kind == ValueKind::Bool && value.boolean == expected;
}

bool is_text(const Value& value, std::string_view expected) {
    return value.kind == ValueKind::Text && value.text == expected;
}

bool is_date(const Value& value, const DateValue& expected) {
    return value.kind == ValueKind::Date && value.date == expected;
}

bool is_null(const Value& value) { return value.is_null(); }

void check_bytes(TestContext& ctx, const EncodedValue& encoded,
                 std::initializer_list<int> expected, const std::string& what) {
    ctx.check(encoded.ok, what + " encodes");
    if (!encoded.ok) {
        return;
    }
    ctx.equal(encoded.bytes.size(), expected.size(), what + " block length");
    std::size_t index = 0;
    for (int byte : expected) {
        if (index >= encoded.bytes.size()) {
            break;
        }
        ctx.check(encoded.bytes[index] == static_cast<std::uint8_t>(byte),
                  what + " byte " + std::to_string(index));
        ++index;
    }
}

/* The registry is the source of ids beyond the six measured anchors. */
FieldId custom_number(int index) {
    const CustomField* field = core::formula::find_custom_field(FieldKind::Number, index);
    return field == nullptr ? 0 : field->field;
}

/*
 * Deep-nesting fixtures.  The shapes match the wave-13 fuzz harness
 * (tests/fuzz/fuzz_parsers_main.cpp): `((((1))))` and
 * `IIf(1,0,IIf(1,0,...))`, plus a right-nested arithmetic chain, a right-nested
 * And chain and a unary chain.
 */
std::string nested_parens(int depth) {
    std::string out(static_cast<std::size_t>(depth), '(');
    out += "1";
    out.append(static_cast<std::size_t>(depth), ')');
    return out;
}

std::string nested_iif(int depth) {
    std::string out;
    for (int index = 0; index < depth; ++index) {
        out += "IIf(1,0,";
    }
    out += "0";
    out.append(static_cast<std::size_t>(depth), ')');
    return out;
}

std::string nested_arithmetic(int depth) {
    std::string out;
    for (int index = 0; index < depth; ++index) {
        out += "1+(";
    }
    out += "1";
    out.append(static_cast<std::size_t>(depth), ')');
    return out;
}

std::string nested_and(int depth) {
    std::string out;
    for (int index = 0; index < depth; ++index) {
        out += "1 And (";
    }
    out += "1";
    out.append(static_cast<std::size_t>(depth), ')');
    return out;
}

std::string unary_chain(int depth, std::string_view op) {
    std::string out;
    for (int index = 0; index < depth; ++index) {
        out += op;
        out += ' ';
    }
    out += "True";
    return out;
}

/*
 * Three tasks.  Dates are the oracle's own measured one (2026-10-26 08:00 is
 * Monday, per the GetField read-back in w10-oracle.md); durations use the
 * measured 4800 ticks per day.
 */
RecordSet make_tasks() {
    RecordSet records(RecordType::Task);
    for (int row = 0; row < 3; ++row) {
        records.add_row(static_cast<std::uint32_t>(row + 1));
    }
    records.set_field(0, task_field("Name"), Value::of_text("Design"));
    records.set_field(1, task_field("Name"), Value::of_text("Build"));
    records.set_field(2, task_field("Name"), Value::of_text("Test"));

    records.set_field(0, task_field("Duration"), Value::of_duration(4800));
    records.set_field(1, task_field("Duration"), Value::of_duration(9600));
    records.set_field(2, task_field("Duration"), Value::of_duration(2400));

    records.set_field(0, task_field("Start"), Value::of_date(date_of(2026, 10, 26, 8, 0)));
    records.set_field(1, task_field("Start"), Value::of_date(date_of(2026, 11, 2, 8, 0)));
    records.set_field(2, task_field("Start"), Value::of_date(date_of(2026, 10, 28, 8, 0)));

    records.set_field(0, task_field("Finish"), Value::of_date(date_of(2026, 10, 30, 8, 0)));
    records.set_field(1, task_field("Finish"), Value::of_date(date_of(2026, 11, 6, 8, 0)));
    records.set_field(2, task_field("Finish"), Value::of_date(date_of(2026, 10, 29, 8, 0)));

    records.set_field(0, task_field("PercentComplete"), Value::of_int(25));
    records.set_field(1, task_field("PercentComplete"), Value::of_int(75));
    records.set_field(2, task_field("PercentComplete"), Value::of_int(50));

    records.set_field(0, core::formula::kTaskNumber1, Value::of_number(42.5));
    records.set_field(1, core::formula::kTaskNumber1, Value::of_number(10.0));
    records.set_field(0, core::formula::kTaskText1, Value::of_text("alpha"));
    records.set_field(1, core::formula::kTaskText1, Value::of_text("beta"));
    records.set_field(0, core::formula::kTaskFlag1, Value::of_bool(true));
    records.set_field(1, core::formula::kTaskFlag1, Value::of_bool(false));
    return records;
}

/* ------------------------------------------------------------------------- */
/* Tokenizer                                                                  */
/* ------------------------------------------------------------------------- */

void test_tokenizer(TestContext& ctx) {
    const core::formula::TokenizeResult r = core::formula::tokenize("1+2");
    ctx.check(r.ok, "[inference] tokenize 1+2 succeeds");
    ctx.equal(r.tokens.size(), std::size_t(4), "[inference] 1+2 has four tokens (with end)");
    if (r.tokens.size() == 4) {
        ctx.check(r.tokens[0].kind == TokenKind::Number, "[inference] first token is a number");
        ctx.equal(r.tokens[0].position, std::size_t(0), "[inference] number position 0");
        ctx.check(r.tokens[1].kind == TokenKind::Plus, "[inference] second token is '+'");
        ctx.check(r.tokens[2].kind == TokenKind::Number, "[inference] third token is a number");
        ctx.check(r.tokens[3].kind == TokenKind::End, "[inference] last token is end");
        ctx.equal(r.tokens[3].position, std::size_t(3), "[inference] end position is length");
    }

    const core::formula::TokenizeResult field_tokens =
        core::formula::tokenize("[MSPJ188744967]*2");
    ctx.check(field_tokens.ok, "[measured] the stored [MSPJ...] token form tokenizes");
    if (field_tokens.ok && field_tokens.tokens.size() >= 3) {
        ctx.check(field_tokens.tokens[0].kind == TokenKind::FieldRef,
                  "[measured] [MSPJ188744967] is a field reference");
        ctx.equal(field_tokens.tokens[0].text, std::string("MSPJ188744967"),
                  "[measured] the field token keeps its decimal id text");
        ctx.equal(field_tokens.tokens[1].position, std::size_t(15),
                  "[measured] the '*' follows the 15-byte bracketed token");
        ctx.equal(field_tokens.tokens[2].number, 2.0, "[inference] the numeric literal is 2");
    }

    const core::formula::TokenizeResult durations = core::formula::tokenize("2d + 1.5 days");
    ctx.check(durations.ok, "[inference] duration literals tokenize");
    if (durations.ok && durations.tokens.size() >= 4) {
        ctx.check(durations.tokens[0].kind == TokenKind::Duration,
                  "[inference] 2d is a duration literal");
        ctx.equal(durations.tokens[0].ticks, std::int64_t(9600),
                  "[inference] 2d is 9600 ticks at 4800 ticks/day");
        ctx.equal(durations.tokens[2].ticks, std::int64_t(7200),
                  "[inference] 1.5 days is 7200 ticks");
    }

    const core::formula::TokenizeResult unit = core::formula::tokenize("4h");
    ctx.check(unit.ok && !unit.tokens.empty() && unit.tokens[0].kind == TokenKind::Duration,
              "[inference] 4h is a duration literal");
    if (unit.ok && !unit.tokens.empty()) {
        ctx.equal(unit.tokens[0].ticks, std::int64_t(2400),
                  "[inference] 4h is 2400 ticks (600/hour)");
    }

    const core::formula::TokenizeResult quotes = core::formula::tokenize("\"a\"\"b\"");
    ctx.check(quotes.ok, "[inference] a doubled quote is one escaped quote");
    if (quotes.ok && !quotes.tokens.empty()) {
        ctx.equal(quotes.tokens[0].text, std::string("a\"b"),
                  "[inference] \"a\"\"b\" reads a\"b");
    }

    const core::formula::TokenizeResult date_token = core::formula::tokenize("#2026-10-26#");
    ctx.check(date_token.ok, "[inference] #...# date literals tokenize");
    if (date_token.ok && !date_token.tokens.empty()) {
        ctx.check(date_token.tokens[0].kind == TokenKind::Date, "[inference] it is a Date token");
        ctx.equal(date_token.tokens[0].date.year, 2026, "[inference] date literal year");
        ctx.equal(static_cast<int>(date_token.tokens[0].date.month), 10,
                  "[inference] date literal month");
        ctx.equal(static_cast<int>(date_token.tokens[0].date.day), 26,
                  "[inference] date literal day");
    }

    const core::formula::TokenizeResult booleans = core::formula::tokenize("True And FALSE");
    ctx.check(booleans.ok, "[inference] boolean literals tokenize");
    if (booleans.ok && booleans.tokens.size() >= 3) {
        ctx.check(booleans.tokens[0].kind == TokenKind::Bool && booleans.tokens[0].boolean,
                  "[inference] True is a boolean literal");
        ctx.check(booleans.tokens[1].kind == TokenKind::Identifier,
                  "[inference] And is an identifier keyword");
        ctx.check(booleans.tokens[2].kind == TokenKind::Bool && !booleans.tokens[2].boolean,
                  "[inference] FALSE is the false literal");
    }

    const core::formula::TokenizeResult bad_string = core::formula::tokenize("\"abc");
    ctx.check(!bad_string.ok, "[inference] an unterminated string is an error");
    ctx.equal(bad_string.error_position, std::size_t(0),
              "[inference] the unterminated string points at the opening quote");
    ctx.contains(bad_string.error, "unterminated string", "[inference] the message says so");

    const core::formula::TokenizeResult bad_char = core::formula::tokenize("1 @ 2");
    ctx.check(!bad_char.ok, "[inference] an unknown character is an error");
    ctx.equal(bad_char.error_position, std::size_t(2),
              "[inference] the bad character position is exact");
    ctx.contains(bad_char.error, "unexpected character", "[inference] the message says so");

    const core::formula::TokenizeResult bad_field = core::formula::tokenize("[Name");
    ctx.check(!bad_field.ok, "[inference] an unterminated field reference is an error");
    ctx.equal(bad_field.error_position, std::size_t(0), "[inference] it points at '['");
    ctx.contains(bad_field.error, "unterminated field reference", "[inference] the message");

    const core::formula::TokenizeResult empty_field = core::formula::tokenize("[]");
    ctx.check(!empty_field.ok, "[inference] an empty field reference is an error");
    ctx.contains(empty_field.error, "empty field reference", "[inference] the message");

    const core::formula::TokenizeResult bad_date = core::formula::tokenize("#not-a-date#");
    ctx.check(!bad_date.ok, "[inference] an invalid date literal is an error");
    ctx.contains(bad_date.error, "invalid date literal", "[inference] the message");
}

/* ------------------------------------------------------------------------- */
/* Parser                                                                     */
/* ------------------------------------------------------------------------- */

void test_parser(TestContext& ctx) {
    core::formula::ParseResult r = core::formula::parse("1+2*3");
    ctx.check(r.ok, "[inference] 1+2*3 parses");
    ctx.check(r.root.kind == core::formula::ExprKind::Binary, "[inference] the root is binary");
    ctx.check(r.root.binary == BinaryOp::Add, "[inference] '+' is the root operator");
    if (r.root.children.size() == 2) {
        ctx.check(r.root.children[1].kind == core::formula::ExprKind::Binary &&
                      r.root.children[1].binary == BinaryOp::Multiply,
                  "[inference] '*' binds tighter than '+'");
    }

    r = core::formula::parse("(1+2)*3");
    ctx.check(r.ok, "[inference] (1+2)*3 parses");
    ctx.check(r.root.binary == BinaryOp::Multiply, "[inference] parentheses change the root");

    r = core::formula::parse("[Duration]*2");
    ctx.check(r.ok, "[inference] a bracketed field name parses");
    ctx.equal(r.field_refs.size(), std::size_t(1), "[inference] one field reference recorded");
    if (!r.field_refs.empty()) {
        ctx.check(r.field_refs[0].field == core::formula::kTaskDurationField,
                  "[measured] [Duration] resolves to Task.Duration (0x0b40001d)");
        ctx.equal(r.field_refs[0].token, std::string("Duration"),
                  "[inference] the reference keeps its source text");
    }

    r = core::formula::parse("[MSPJ188744967]*2");
    ctx.check(r.ok, "[measured] the stored [MSPJ188744967] form parses");
    if (!r.field_refs.empty()) {
        ctx.check(r.field_refs[0].field == core::formula::kTaskDurationTextField,
                  "[measured] 188744967 is Task.DurationText (0x0b400507)");
    }

    r = core::formula::parse("[% Complete]>50");
    ctx.check(r.ok, "[inference] the formula UI spelling [% Complete] resolves");
    if (!r.field_refs.empty()) {
        ctx.check(r.field_refs[0].field == core::formula::kTaskPercentCompleteField,
                  "[measured] [% Complete] is Task.PercentComplete (0x0b400020)");
    }

    r = core::formula::parse("[Name] & \"-\" & [Duration]");
    ctx.check(r.ok, "[measured] the measured text-concat formula parses");
    ctx.equal(r.field_refs.size(), std::size_t(2), "[measured] two field references");
    if (r.field_refs.size() == 2) {
        ctx.check(r.field_refs[0].field == task_field("Name"), "[measured] the first is Name");
        ctx.check(r.field_refs[1].field == core::formula::kTaskDurationField,
                  "[measured] the second is Duration");
    }

    r = core::formula::parse("IIf([% Complete]>50,1,0)");
    ctx.check(r.ok, "[measured] the measured IIf formula parses");
    ctx.check(r.root.kind == core::formula::ExprKind::Call, "[inference] IIf is a call node");
    ctx.equal(r.root.text, std::string("iif"), "[inference] the call name is normalised");
    ctx.equal(r.root.children.size(), std::size_t(3), "[inference] IIf has three arguments");

    r = core::formula::parse("iif(1,2,3)");
    ctx.check(r.ok, "[inference] function names are case-insensitive");

    r = core::formula::parse("2d + 1h");
    ctx.check(r.ok, "[inference] duration arithmetic parses");
    if (r.root.children.size() == 2) {
        ctx.check(r.root.children[0].kind == core::formula::ExprKind::Duration,
                  "[inference] 2d is a duration literal node");
        ctx.equal(r.root.children[0].ticks, std::int64_t(9600),
                  "[inference] 2d node carries 9600 ticks");
        ctx.equal(r.root.children[1].ticks, std::int64_t(600),
                  "[inference] 1h node carries 600 ticks");
    }

    r = core::formula::parse("#2026-10-26# + 7");
    ctx.check(r.ok, "[inference] a date literal in arithmetic parses");
    if (!r.root.children.empty()) {
        ctx.check(r.root.children[0].kind == core::formula::ExprKind::Date,
                  "[inference] the date literal is a Date node");
    }

    r = core::formula::parse("1 And 0 Or 1");
    ctx.check(r.ok, "[inference] And/Or parse");
    ctx.check(r.root.binary == BinaryOp::Or, "[inference] Or is the lowest precedence");
    if (r.root.children.size() == 2) {
        ctx.check(r.root.children[0].kind == core::formula::ExprKind::Binary &&
                      r.root.children[0].binary == BinaryOp::And,
                  "[inference] And binds tighter than Or");
    }

    r = core::formula::parse("Not 1");
    ctx.check(r.ok && r.root.kind == core::formula::ExprKind::Unary,
              "[inference] Not is a unary operator");

    r = core::formula::parse("True");
    ctx.check(r.ok && r.root.kind == core::formula::ExprKind::Bool,
              "[inference] a bare boolean literal parses");

    r = core::formula::parse("\"x\" & \"y\"");
    ctx.check(r.ok && r.root.binary == BinaryOp::Concat, "[inference] & is concatenation");
}

void test_parse_errors(TestContext& ctx) {
    core::formula::ParseResult r = core::formula::parse("");
    ctx.check(!r.ok, "[inference] an empty formula is an error");
    ctx.equal(r.error_position, std::size_t(0), "[inference] the empty formula points at 0");
    ctx.contains(r.error, "expected an expression", "[inference] the message");

    r = core::formula::parse("[NoSuchField]");
    ctx.check(!r.ok, "[inference] an unknown field name is an error");
    ctx.equal(r.error_position, std::size_t(0), "[inference] it points at the '['");
    ctx.contains(r.error, "unknown field reference", "[inference] the message names the field");

    r = core::formula::parse("[MSPJ999999]");
    ctx.check(!r.ok, "[inference] an MSPJ id outside the descriptor table is an error");
    ctx.contains(r.error, "unknown field reference", "[inference] the message");

    r = core::formula::parse("Foo(1)");
    ctx.check(!r.ok, "[inference] an unknown function is an error");
    ctx.equal(r.error_position, std::size_t(0), "[inference] it points at the function name");
    ctx.contains(r.error, "unknown function", "[inference] the message names the function");

    r = core::formula::parse("IIf(1,2)");
    ctx.check(!r.ok, "[inference] a wrong argument count is an error");
    ctx.contains(r.error, "takes 3", "[inference] the message states the arity");

    r = core::formula::parse("1 +");
    ctx.check(!r.ok, "[inference] a dangling operator is an error");
    ctx.equal(r.error_position, std::size_t(3), "[inference] it points past the end");
    ctx.contains(r.error, "expected an expression", "[inference] the message");

    r = core::formula::parse("(1+2");
    ctx.check(!r.ok, "[inference] a missing ')' is an error");
    ctx.equal(r.error_position, std::size_t(4), "[inference] it points at the end");
    ctx.contains(r.error, "expected ')'", "[inference] the message");

    r = core::formula::parse("1 2");
    ctx.check(!r.ok, "[inference] trailing junk is an error");
    ctx.equal(r.error_position, std::size_t(2), "[inference] it points at the second number");
    ctx.contains(r.error, "unexpected", "[inference] the message");

    r = core::formula::parse("1,2");
    ctx.check(!r.ok, "[inference] a comma outside a call is an error");
    ctx.equal(r.error_position, std::size_t(1), "[inference] it points at the comma");

    r = core::formula::parse("Duration");
    ctx.check(!r.ok, "[inference] a bare name is not a field reference");
    ctx.contains(r.error, "[Name]", "[inference] the message says how to write a field");

    ctx.contains(r.error, "offset 0", "[inference] parse errors carry an offset");
}

/* ------------------------------------------------------------------------- */
/* Nesting/recursion guard (wave-13 fuzz follow-up; ASan follow-up)           */
/*                                                                            */
/* Before the guard, parse() on 256 nested IIf or 384 nested parentheses      */
/* died with 0xC00000FD in MSVC Release (formula text comes from 114/Props).  */
/* The bound is 64, not 128/160: under /fsanitize=address the larger frames   */
/* overflow at ~80, so 64 is the depth that is safe on every build and thread */
/* (real formulas are fewer than ten levels deep).  Nesting up to and         */
/* including 64 is accepted; 65 is the first refused level.  The constant is  */
/* shared with the evaluator.  Fixtures match tests/fuzz/fuzz_parsers_main.cpp. */
/* ------------------------------------------------------------------------- */

void test_nesting_limits(TestContext& ctx) {
    ctx.equal(core::formula::kMaxFormulaDepth, 64,
              "[measured] the shared nesting bound is 64 (ASan: 72 parsed, 80 overflowed)");
    const std::string limit_text = std::to_string(core::formula::kMaxFormulaDepth);

    /* ---- 48 and 64 deep are accepted; 64 is the last accepted level ---- */
    for (int depth : {48, 64}) {
        const std::string label = std::to_string(depth) + " nested parentheses";
        const core::formula::ParseResult parsed = core::formula::parse(nested_parens(depth));
        ctx.check(parsed.ok, "[measured] " + label + " parse (within the guard)");
        if (parsed.ok) {
            EvalContext context;
            const core::formula::EvalResult result =
                core::formula::evaluate(parsed.root, context);
            ctx.check(result.ok && is_number(result.value, 1.0),
                      "[inference] " + label + " still evaluate");
        }
    }
    for (int depth : {48, 64}) {
        const std::string label = std::to_string(depth) + " nested IIf calls";
        const core::formula::ParseResult parsed = core::formula::parse(nested_iif(depth));
        ctx.check(parsed.ok, "[measured] " + label + " parse (within the guard)");
        if (parsed.ok) {
            EvalContext context;
            const core::formula::EvalResult result =
                core::formula::evaluate(parsed.root, context);
            ctx.check(result.ok && is_number(result.value, 0.0),
                      "[inference] " + label + " evaluate (lazy false branch)");
        }
    }

    /* ---- 65 is the first refused level ---- */
    for (int depth : {65}) {
        const std::string label = std::to_string(depth) + " nested parentheses";
        const std::string text = nested_parens(depth);
        const core::formula::ParseResult parsed = core::formula::parse(text);
        ctx.check(!parsed.ok, "[measured] " + label + " are refused (first level past the bound)");
        ctx.check(parsed.error_position <= text.size(),
                  "[inference] the " + label + " refusal is positioned");
        ctx.contains(parsed.error, "nesting exceeds " + limit_text + " levels",
                     "[inference] the " + label + " error names the bound");
        const core::formula::ParseResult parsed_iif =
            core::formula::parse(nested_iif(depth));
        ctx.check(!parsed_iif.ok,
                  "[measured] 65 nested IIf calls are refused (first level past the bound)");
    }

    /* ---- deeper input is refused with a positioned error, never a crash ---- */
    const int deep_depths[] = {65, 128, 256, 384, 1024, 4096};
    for (int depth : deep_depths) {
        const std::string text = nested_parens(depth);
        const std::string label = std::to_string(depth) + " nested parentheses";
        const core::formula::ParseResult parsed = core::formula::parse(text);
        ctx.check(!parsed.ok, "[measured] " + label + " are refused, not parsed");
        ctx.check(parsed.error_position <= text.size(),
                  "[inference] the " + label + " refusal is positioned");
        ctx.contains(parsed.error, "nesting exceeds " + limit_text + " levels",
                     "[inference] the " + label + " error names the bound");
        ctx.contains(parsed.error, "offset ",
                     "[inference] the " + label + " error carries an offset");
    }
    for (int depth : {128, 256, 1024}) {
        const std::string text = nested_iif(depth);
        const std::string label = std::to_string(depth) + " nested IIf calls";
        const core::formula::ParseResult parsed = core::formula::parse(text);
        ctx.check(!parsed.ok, "[measured] " + label + " are refused, not parsed");
        ctx.check(parsed.error_position <= text.size(),
                  "[inference] the " + label + " refusal is positioned");
        ctx.contains(parsed.error, "nesting exceeds " + limit_text + " levels",
                     "[inference] the " + label + " error names the bound");
    }

    /* ---- unbalanced input still reports its own syntax error ---- */
    const std::string unbalanced = std::string(48, '(') + "1";
    const core::formula::ParseResult un = core::formula::parse(unbalanced);
    ctx.check(!un.ok, "[inference] a deep but unbalanced formula is an error");
    ctx.contains(un.error, "expected ')'",
                 "[inference] the unbalanced error is the syntax error, not the guard");
    ctx.check(un.error_position <= unbalanced.size(),
              "[inference] the unbalanced error is positioned");

    /* ---- right-nested arithmetic and And chains ---- */
    const core::formula::ParseResult arithmetic_ok = core::formula::parse(nested_arithmetic(64));
    ctx.check(arithmetic_ok.ok,
              "[inference] a 64-deep right-nested arithmetic expression parses");
    if (arithmetic_ok.ok) {
        EvalContext context;
        const core::formula::EvalResult result =
            core::formula::evaluate(arithmetic_ok.root, context);
        ctx.check(result.ok && is_number(result.value, 65.0),
                  "[inference] the 64-deep arithmetic chain evaluates to 65");
    }
    const core::formula::ParseResult arithmetic_deep =
        core::formula::parse(nested_arithmetic(128));
    ctx.check(!arithmetic_deep.ok,
              "[inference] a 128-deep right-nested arithmetic expression is refused");
    ctx.contains(arithmetic_deep.error, "nesting exceeds", "[inference] with the depth error");

    ctx.check(core::formula::parse(nested_and(64)).ok,
              "[inference] a 64-deep right-nested And chain parses");
    const core::formula::ParseResult and_deep = core::formula::parse(nested_and(128));
    ctx.check(!and_deep.ok, "[inference] a 128-deep right-nested And chain is refused");
    ctx.contains(and_deep.error, "nesting exceeds", "[inference] with the depth error");

    /* ---- unary chains recurse without entering parse_or, so also bound ---- */
    const core::formula::ParseResult unary_ok =
        core::formula::parse(unary_chain(64, "Not"));
    ctx.check(unary_ok.ok, "[inference] a 64-long unary chain parses");
    if (unary_ok.ok) {
        EvalContext context;
        const core::formula::EvalResult result = core::formula::evaluate(unary_ok.root, context);
        ctx.check(result.ok && is_bool(result.value, true),
                  "[inference] the 64-long unary chain evaluates");
    }
    const core::formula::ParseResult unary_boundary =
        core::formula::parse(unary_chain(65, "Not"));
    ctx.check(!unary_boundary.ok,
              "[inference] a 65-long unary chain is refused (same boundary as parens)");
    const core::formula::ParseResult unary_deep =
        core::formula::parse(unary_chain(128, "Not"));
    ctx.check(!unary_deep.ok, "[inference] a 128-long unary chain is refused");
    ctx.contains(unary_deep.error, "nesting exceeds", "[inference] with the depth error");

    /* ---- flat chains: parsed iteratively, refused once the TREE gets deep ---- */
    std::string flat = "1";
    for (int index = 0; index < 32; ++index) {
        flat += " And 1";
    }
    const core::formula::ParseResult short_flat = core::formula::parse(flat);
    ctx.check(short_flat.ok, "[inference] a 32-term flat And chain parses");
    if (short_flat.ok) {
        EvalContext context;
        const core::formula::EvalResult result =
            core::formula::evaluate(short_flat.root, context);
        ctx.check(result.ok && is_bool(result.value, true),
                  "[inference] the 32-term flat chain evaluates");
    }
    flat = "1";
    for (int index = 0; index < 4096; ++index) {
        flat += " And 1";
    }
    const core::formula::ParseResult flat_parsed = core::formula::parse(flat);
    ctx.check(!flat_parsed.ok,
              "[measured] a 4096-term flat And chain is refused at parse time "
              "(its left-deep AST would overflow on destruction/evaluation)");
    ctx.check(flat_parsed.error_position <= flat.size(),
              "[inference] the flat-chain refusal is positioned");
    ctx.contains(flat_parsed.error, "nesting exceeds " + limit_text + " levels",
                 "[inference] the flat-chain error names the bound");

    /* ---- the evaluator refuses a deep hand-built AST (no parser involved) ---- */
    core::formula::Expr deep_expr;
    deep_expr.kind = core::formula::ExprKind::Bool;
    deep_expr.boolean = true;
    for (int index = 0; index < 80; ++index) {
        core::formula::Expr parent;
        parent.kind = core::formula::ExprKind::Unary;
        parent.unary = core::formula::UnaryOp::Not;
        parent.children.push_back(std::move(deep_expr));
        deep_expr = std::move(parent);
    }
    EvalContext context;
    const core::formula::EvalResult deep_result = core::formula::evaluate(deep_expr, context);
    ctx.check(!deep_result.ok, "[inference] an 80-deep hand-built AST is refused");
    ctx.contains(deep_result.error, "nesting exceeds " + limit_text + " levels",
                 "[inference] the evaluator's depth error names the bound");

    core::formula::Expr boundary_expr;
    boundary_expr.kind = core::formula::ExprKind::Bool;
    boundary_expr.boolean = true;
    for (int index = 0; index < 65; ++index) {
        core::formula::Expr parent;
        parent.kind = core::formula::ExprKind::Unary;
        parent.unary = core::formula::UnaryOp::Not;
        parent.children.push_back(std::move(boundary_expr));
        boundary_expr = std::move(parent);
    }
    const core::formula::EvalResult boundary_result =
        core::formula::evaluate(boundary_expr, context);
    ctx.check(!boundary_result.ok, "[inference] a 65-deep hand-built AST is refused");

    core::formula::Expr shallow_expr;
    shallow_expr.kind = core::formula::ExprKind::Bool;
    shallow_expr.boolean = true;
    for (int index = 0; index < 64; ++index) {
        core::formula::Expr parent;
        parent.kind = core::formula::ExprKind::Unary;
        parent.unary = core::formula::UnaryOp::Not;
        parent.children.push_back(std::move(shallow_expr));
        shallow_expr = std::move(parent);
    }
    const core::formula::EvalResult shallow_result =
        core::formula::evaluate(shallow_expr, context);
    ctx.check(shallow_result.ok && is_bool(shallow_result.value, true),
              "[inference] a 64-deep hand-built AST still evaluates");
}

/* ------------------------------------------------------------------------- */
/* Evaluator: arithmetic, coercion, null handling                             */
/* ------------------------------------------------------------------------- */

void test_arithmetic(TestContext& ctx) {
    const RecordSet records = make_tasks();

    ctx.check(is_number(eval_at(records, 0, "1+2"), 3.0),
              "[inference] 1+2 is Number 3 (numeric literals are Numbers)");
    ctx.check(is_number(eval_at(records, 0, "1.5+2"), 3.5), "[inference] 1.5+2 is Number 3.5");
    ctx.check(is_number(eval_at(records, 0, "1/2"), 0.5), "[inference] 1/2 is Number 0.5");
    ctx.check(is_null(eval_at(records, 0, "1/0")), "[inference] division by zero is Null");
    ctx.check(is_number(eval_at(records, 0, "2*3"), 6.0), "[inference] 2*3 is Number 6");
    ctx.check(is_number(eval_at(records, 0, "-7+2"), -5.0), "[inference] unary minus and add");

    ctx.check(is_duration(eval_at(records, 0, "[Duration]*2"), 9600),
              "[inference] [Duration]*2 doubles the ticks");
    ctx.check(is_duration(eval_at(records, 1, "[Duration]*2"), 19200),
              "[inference] [Duration]*2 on the two-day row");
    ctx.check(is_duration(eval_at(records, 0, "[Duration]/2"), 2400),
              "[inference] [Duration]/2 halves the ticks");
    ctx.check(is_number(eval_at(records, 0, "[Duration]/[Duration]"), 1.0),
              "[inference] duration / duration is a Number ratio");
    ctx.check(is_duration(eval_at(records, 0, "[Duration]+1d"), 9600),
              "[inference] duration + 1d adds ticks");
    ctx.check(is_duration(eval_at(records, 0, "[Duration]+1"), 9600),
              "[inference] a bare number in duration arithmetic means days");
    ctx.check(is_duration(eval_at(records, 0, "4800*2d"), 9600 * 4800),
              "[inference] number * duration multiplies the ticks");

    ctx.check(is_number(eval_at(records, 0, "[Number1]*2"), 85.0),
              "[inference] a Number field multiplies");
    ctx.check(is_number(eval_at(records, 0, "[Number1]+1"), 43.5),
              "[inference] a Number field plus an integer");
    ctx.check(is_number(eval_at(records, 0, "[PercentComplete]+[Number1]"), 67.5),
              "[inference] mixed integer/number arithmetic widens to Number");
    ctx.check(is_number(eval_at(records, 0, "-[Number1]"), -42.5),
              "[inference] unary minus applies to a Number field");
    ctx.check(is_number(eval_at(records, 0, "[Flag1]+1"), 2.0),
              "[inference] a Boolean operand counts as 1");
    ctx.check(is_number(eval_at(records, 1, "[Flag1]+1"), 1.0),
              "[inference] a false Boolean operand counts as 0");
    ctx.check(is_number(eval_at(records, 0, "\"8\"+1"), 9.0),
              "[inference] numeric text coerces in arithmetic");
    ctx.check(is_null(eval_at(records, 0, "\"a\"+1")),
              "[inference] non-numeric text in arithmetic is Null");

    ctx.check(is_text(eval_at(records, 0, "\"a\" & \"b\""), "ab"),
              "[inference] & concatenates text");
    ctx.check(is_text(eval_at(records, 0, "[Name] & \"-\" & [Duration]"), "Design-1 day"),
              "[inference] & renders a Duration as its display text");
    ctx.check(is_text(eval_at(records, 0, "[Text1] & [Number1]"), "alpha42.5"),
              "[inference] & renders a Number without trailing zeros");
    ctx.check(is_text(eval_at(records, 0, "[Text2] & \"x\""), "x"),
              "[inference] & treats a blank as empty text");
    ctx.check(is_null(eval_at(records, 0, "[Text2]+1")),
              "[inference] a blank field in arithmetic is Null");

    ctx.check(is_bool(eval_at(records, 0, "[PercentComplete]>50"), false),
              "[inference] [% Complete]>50 is false for 25");
    ctx.check(is_bool(eval_at(records, 1, "[PercentComplete]>50"), true),
              "[inference] [% Complete]>50 is true for 75");
    ctx.check(is_bool(eval_at(records, 0, "[PercentComplete]>=25"), true),
              "[inference] >= compares");
    ctx.check(is_bool(eval_at(records, 1, "[PercentComplete]<>25"), true),
              "[inference] <> compares");
    ctx.check(is_bool(eval_at(records, 0, "[Name]=\"Design\""), true),
              "[inference] text equality compares");
    ctx.check(is_bool(eval_at(records, 0, "\"8\"=8"), true),
              "[inference] comparison uses core/view's coercion (text 8 equals number 8)");
    ctx.check(is_bool(eval_at(records, 0, "[Number1]=42.5"), true),
              "[inference] a Number field compares to a literal");
    ctx.check(is_null(eval_at(records, 0, "[Text1]=[Text2]")),
              "[inference] a comparison with a blank is Null");

    ctx.check(is_number(eval_at(records, 0, "IIf([% Complete]>50,1,0)"), 0.0),
              "[inference] IIf picks 0 below 50");
    ctx.check(is_number(eval_at(records, 1, "IIf([% Complete]>50,1,0)"), 1.0),
              "[inference] IIf picks 1 above 50");
    ctx.check(is_number(eval_at(records, 0, "IIf(1,5,1/0)"), 5.0),
              "[inference] IIf is lazy: the untaken branch is not evaluated");
    ctx.check(is_number(eval_at(records, 0, "IIf([Text2],1,2)"), 2.0),
              "[inference] a blank condition takes the false branch");
    ctx.check(is_bool(eval_at(records, 0, "Not [Flag1]"), false),
              "[inference] Not inverts a true flag");
    ctx.check(is_bool(eval_at(records, 0, "(1=1) And (2=2)"), true),
              "[inference] And of two true comparisons");
    ctx.check(is_bool(eval_at(records, 0, "(1=2) Or (2=2)"), true),
              "[inference] Or of a false and a true comparison");
    ctx.check(is_bool(eval_at(records, 0, "1 And 0"), false),
              "[inference] And treats numbers by truthiness");
}

/* ------------------------------------------------------------------------- */
/* Evaluator: dates and durations                                             */
/* ------------------------------------------------------------------------- */

void test_dates_and_durations(TestContext& ctx) {
    const RecordSet records = make_tasks();

    ctx.check(is_date(eval_at(records, 0, "[Start]+7"), date_of(2026, 11, 2, 8, 0)),
              "[inference] date + 7 is seven days later");
    ctx.check(is_date(eval_at(records, 0, "[Start]+[Duration]"), date_of(2026, 10, 27, 8, 0)),
              "[inference] date + duration adds ticks");
    ctx.check(is_date(eval_at(records, 0, "[Start]-[Duration]"), date_of(2026, 10, 25, 8, 0)),
              "[inference] date - duration subtracts ticks");
    ctx.check(is_duration(eval_at(records, 0, "[Finish]-[Start]"), 19200),
              "[inference] date - date is a Duration (4 days)");
    ctx.check(is_date(eval_at(records, 0, "6+[Start]"), date_of(2026, 11, 1, 8, 0)),
              "[inference] number + date is commutative");
    ctx.check(is_null(eval_at(records, 0, "[Start]*2")),
              "[inference] date multiplication is Null");
    ctx.check(is_null(eval_at(records, 0, "[Start]+[Finish]")),
              "[inference] date + date is Null");

    ctx.check(is_number(eval_at(records, 0, "ProjDurValue([Duration])"), 480.0),
              "[inference] ProjDurValue returns minutes (4800 ticks = 480)");
    ctx.check(is_number(eval_at(records, 0, "ProjDurValue(2d)"), 960.0),
              "[inference] ProjDurValue of a two-day literal");
    ctx.check(is_number(eval_at(records, 0, "ProjDurConv([Duration],\"h\")"), 8.0),
              "[inference] ProjDurConv converts to hours");
    ctx.check(is_number(eval_at(records, 0, "ProjDurConv([Duration],\"m\")"), 480.0),
              "[inference] ProjDurConv converts to minutes");
    ctx.check(is_number(eval_at(records, 0, "ProjDurConv([Duration],\"w\")"), 0.2),
              "[inference] ProjDurConv uses a 5-day week");
    ctx.check(is_null(eval_at(records, 0, "ProjDurConv([Duration],\"bogus\")")),
              "[inference] ProjDurConv with an unknown unit is Null");

    ctx.check(is_date(eval_at(records, 0, "ProjDateAdd([Start],2d)"), date_of(2026, 10, 28, 8, 0)),
              "[inference] ProjDateAdd shifts forward");
    ctx.check(is_date(eval_at(records, 0, "ProjDateSub([Start],1d)"), date_of(2026, 10, 25, 8, 0)),
              "[inference] ProjDateSub shifts back");
    ctx.check(is_duration(eval_at(records, 0, "ProjDateDiff([Finish],[Start])"), 19200),
              "[inference] ProjDateDiff returns a Duration");

    /* 2026-10-26 is a Monday: the oracle's own GetField read-back said
     * "Mon 26/10/26", so the packed date 0x3d1812c0 is measured. */
    const DateValue monday = date_of(2026, 10, 26, 8, 0);
    ctx.check(is_integer(eval_at(records, 0, "Day(#2026-10-26#)"), 26),
              "[inference] Day extracts the day");
    ctx.check(is_integer(eval_at(records, 0, "Month(#2026-10-26#)"), 10),
              "[inference] Month extracts the month");
    ctx.check(is_integer(eval_at(records, 0, "Year(#2026-10-26#)"), 2026),
              "[inference] Year extracts the year");
    ctx.check(is_integer(eval_at(records, 0, "Hour(#2026-10-26T08:00#)"), 8),
              "[inference] Hour extracts the hour");
    ctx.check(is_integer(eval_at(records, 0, "Minute(#2026-10-26T08:00#)"), 0),
              "[inference] Minute extracts the minute");
    ctx.check(is_integer(eval_at(records, 0, "Weekday(#2026-10-26#)"), 2),
              "[measured] 2026-10-26 is Monday, so Weekday is 2 with Sunday=1");
    ctx.check(is_integer(eval_at(records, 0, "Weekday(#2026-10-25#)"), 1),
              "[inference] the Sunday before is 1");
    ctx.check(is_date(eval_at(records, 0, "DateValue(\"2026-10-26\")"), date_of(2026, 10, 26)),
              "[inference] DateValue parses ISO text");
    ctx.check(is_null(eval_at(records, 0, "DateValue(\"nope\")")),
              "[inference] DateValue of bad text is Null");
    ctx.check(is_null(eval_at(records, 0, "Day(\"nope\")")),
              "[inference] a date function on non-date text is Null");
    ctx.check(is_bool(eval_at(records, 0, "IsNull([Text2])"), true),
              "[inference] IsNull is true for an absent field");
    ctx.check(is_bool(eval_at(records, 0, "IsNull([Text1])"), false),
              "[inference] IsNull is false for a present field");

    /* The measured monday value must be the one the packed date decodes to. */
    bool ok = false;
    const DateValue decoded = core::formula::decode_packed_date(0x3d1812c0u, ok);
    ctx.check(ok, "[measured] packed date 0x3d1812c0 decodes");
    ctx.check(decoded == monday, "[measured] 0x3d1812c0 is 2026-10-26 08:00");
}

/* ------------------------------------------------------------------------- */
/* Storage encodings                                                          */
/* ------------------------------------------------------------------------- */

void test_encodings(TestContext& ctx) {
    /* --- Text: UTF-16LE, NUL-terminated (measured) --- */
    const EncodedValue text = core::formula::encode_value(FieldKind::Text,
                                                          Value::of_text("alpha"));
    check_bytes(ctx, text, {'a', 0, 'l', 0, 'p', 0, 'h', 0, 'a', 0, 0, 0},
                "[measured] Text1 \"alpha\" block");
    const DecodedValue text_back = core::formula::decode_value(FieldKind::Text, text.bytes);
    ctx.check(text_back.ok, "[measured] the text block decodes");
    ctx.check(is_text(text_back.value, "alpha"), "[measured] text round-trips");

    const EncodedValue empty_text = core::formula::encode_value(FieldKind::Text,
                                                                Value::of_text(""));
    check_bytes(ctx, empty_text, {0, 0}, "[measured] an empty text block is just the NUL");
    const DecodedValue empty_back = core::formula::decode_value(FieldKind::Text, empty_text.bytes);
    ctx.check(empty_back.ok && empty_back.value.kind == ValueKind::Text &&
                  empty_back.value.text.empty(),
              "[measured] empty text round-trips");

    const std::string unicode = "caf\xc3\xa9 \xe2\x82\xac \xf0\x9d\x84\x9e";
    const EncodedValue unicode_block = core::formula::encode_value(FieldKind::Text,
                                                                   Value::of_text(unicode));
    ctx.check(unicode_block.ok, "[inference] UTF-8 text encodes to UTF-16LE");
    const DecodedValue unicode_back =
        core::formula::decode_value(FieldKind::Text, unicode_block.bytes);
    ctx.check(unicode_back.ok && unicode_back.value.text == unicode,
              "[inference] non-BMP text survives the UTF-16 round-trip");
    ctx.check(unicode_block.bytes.size() % 2 == 0,
              "[inference] a text block is a whole number of UTF-16 units");

    std::string odd;
    const bool odd_ok = core::formula::decode_text_utf16le(
        reinterpret_cast<const std::uint8_t*>("abc"), 3, odd);
    ctx.check(!odd_ok, "[inference] an odd-sized text block is rejected");
    std::string no_nul;
    const std::uint8_t two_bytes[2] = {'a', 0};
    ctx.check(core::formula::decode_text_utf16le(two_bytes, 2, no_nul) && no_nul == "a",
              "[inference] a text block without a NUL is accepted");

    /* --- Number: little-endian f64 (measured) --- */
    const EncodedValue number = core::formula::encode_value(FieldKind::Number,
                                                            Value::of_number(42.5));
    check_bytes(ctx, number, {0, 0, 0, 0, 0, 0x40, 0x45, 0x40},
                "[measured] Number1 42.5 block");
    const DecodedValue number_back = core::formula::decode_value(FieldKind::Number, number.bytes);
    ctx.check(number_back.ok && is_number(number_back.value, 42.5),
              "[measured] 42.5 round-trips");

    const EncodedValue number2 = core::formula::encode_value(FieldKind::Number,
                                                             Value::of_number(3.25));
    check_bytes(ctx, number2, {0, 0, 0, 0, 0, 0, 0x0a, 0x40},
                "[measured] Number2 3.25 block");
    const DecodedValue number2_back =
        core::formula::decode_value(FieldKind::Number, number2.bytes);
    ctx.check(number2_back.ok && is_number(number2_back.value, 3.25),
              "[measured] 3.25 round-trips");

    const EncodedValue one = core::formula::encode_value(FieldKind::Number,
                                                         Value::of_number(1.0));
    check_bytes(ctx, one, {0, 0, 0, 0, 0, 0, 0xf0, 0x3f},
                "[measured] an f64 1.0 is little-endian 3ff0");
    const EncodedValue from_int = core::formula::encode_value(FieldKind::Number,
                                                              Value::of_int(7));
    const DecodedValue from_int_back =
        core::formula::decode_value(FieldKind::Number, from_int.bytes);
    ctx.check(from_int.ok && from_int_back.ok && is_number(from_int_back.value, 7.0),
              "[inference] an Integer encodes as an f64");

    /* --- Duration: u32 file ticks, 4800 = 1 day (measured) --- */
    const EncodedValue duration = core::formula::encode_value(FieldKind::Duration,
                                                              Value::of_duration(4800));
    check_bytes(ctx, duration, {0xc0, 0x12, 0, 0}, "[measured] Duration1 4800-tick block");
    const DecodedValue duration_back =
        core::formula::decode_value(FieldKind::Duration, duration.bytes);
    ctx.check(duration_back.ok && is_duration(duration_back.value, 4800),
              "[measured] 4800 ticks round-trip");

    const EncodedValue bad_duration = core::formula::encode_value(FieldKind::Duration,
                                                                  Value::of_int(4800));
    ctx.check(!bad_duration.ok, "[inference] a bare number is not accepted as Duration storage");
    const EncodedValue negative = core::formula::encode_value(FieldKind::Duration,
                                                              Value::of_duration(-1));
    ctx.check(!negative.ok, "[inference] a negative duration does not fit the u32 field");
    const EncodedValue too_big = core::formula::encode_value(
        FieldKind::Duration, Value::of_duration(std::int64_t(0x100000000)));
    ctx.check(!too_big.ok, "[inference] a duration above u32 is rejected");

    ctx.equal(core::formula::duration_ticks_to_com_minutes(4800), std::int64_t(480),
              "[measured] 4800 file ticks read as 480 COM minutes");
    ctx.equal(core::formula::com_minutes_to_duration_ticks(480), std::int64_t(4800),
              "[measured] 480 COM minutes store as 4800 file ticks");
    ctx.equal(core::formula::duration_ticks_to_com_minutes(2400), std::int64_t(240),
              "[measured] the x10 boundary holds for 2400 ticks");

    /* --- Date: packed (day << 16) | seconds/6, day 0 = 1983-12-31 (measured) --- */
    const EncodedValue packed = core::formula::encode_value(
        FieldKind::Date, Value::of_date(date_of(2026, 10, 26, 8, 0)));
    check_bytes(ctx, packed, {0xc0, 0x12, 0x18, 0x3d}, "[measured] Date1 packed block");
    const DecodedValue packed_back = core::formula::decode_value(FieldKind::Date, packed.bytes);
    ctx.check(packed_back.ok && packed_back.value.kind == ValueKind::Date &&
                  packed_back.value.date == date_of(2026, 10, 26, 8, 0),
              "[measured] packed date round-trips");

    bool epoch_ok = false;
    const std::uint32_t epoch = core::formula::encode_packed_date(
        date_of(1983, 12, 31), epoch_ok);
    ctx.check(epoch_ok && epoch == 0u, "[measured] day 0 is 1983-12-31");
    bool decade_ok = false;
    ctx.check(core::formula::decode_packed_date(0x3d1812c0u, decade_ok) ==
                      date_of(2026, 10, 26, 8, 0) &&
                  decade_ok,
              "[measured] the measured raw date is 2026-10-26 08:00");
    bool bad_packed_ok = true;
    (void)core::formula::decode_packed_date(0xffffffffu, bad_packed_ok);
    ctx.check(!bad_packed_ok, "[measured] 0xffffffff is not a date");
    bool invalid_date_ok = true;
    (void)core::formula::encode_packed_date(date_of(0, 0, 0), invalid_date_ok);
    ctx.check(!invalid_date_ok, "[inference] an invalid DateValue is rejected");

    /* --- Flag: OUR CHOICE, u32 0/1 (unmeasured) --- */
    const EncodedValue flag = core::formula::encode_value(FieldKind::Flag,
                                                          Value::of_bool(true));
    check_bytes(ctx, flag, {1, 0, 0, 0}, "[inference] flag true is u32 1 (our choice)");
    const DecodedValue flag_back = core::formula::decode_value(FieldKind::Flag, flag.bytes);
    ctx.check(flag_back.ok && is_bool(flag_back.value, true),
              "[inference] flag true round-trips");
    const EncodedValue flag_false = core::formula::encode_value(FieldKind::Flag,
                                                                Value::of_bool(false));
    check_bytes(ctx, flag_false, {0, 0, 0, 0}, "[inference] flag false is u32 0 (our choice)");
    std::vector<std::uint8_t> any_nonzero = {2, 0, 0, 0};
    const DecodedValue flag_any = core::formula::decode_value(FieldKind::Flag, any_nonzero);
    ctx.check(flag_any.ok && is_bool(flag_any.value, true),
              "[inference] any non-zero u32 decodes as a set flag");
    const EncodedValue bad_flag = core::formula::encode_value(FieldKind::Flag,
                                                              Value::of_int(7));
    ctx.check(!bad_flag.ok, "[inference] a flag must be 0/1");

    /* --- Cost: OUR CHOICE, f64 (unmeasured) --- */
    const EncodedValue cost = core::formula::encode_value(FieldKind::Cost,
                                                          Value::of_number(1234.5));
    ctx.check(cost.ok && cost.bytes.size() == 8,
              "[inference] cost storage is an 8-byte f64 (our choice)");
    const DecodedValue cost_back = core::formula::decode_value(FieldKind::Cost, cost.bytes);
    ctx.check(cost_back.ok && is_number(cost_back.value, 1234.5),
              "[inference] cost round-trips");

    /* --- block sizes, confidence and wrong-size inputs --- */
    ctx.equal(core::formula::storage_block_size(FieldKind::Text), std::size_t(0),
              "[measured] Text is the only variable-length block");
    ctx.equal(core::formula::storage_block_size(FieldKind::Number), std::size_t(8),
              "[measured] Number block is 8 bytes");
    ctx.equal(core::formula::storage_block_size(FieldKind::Duration), std::size_t(4),
              "[measured] Duration block is 4 bytes");
    ctx.equal(core::formula::storage_block_size(FieldKind::Date), std::size_t(4),
              "[measured] Date block is 4 bytes");
    ctx.equal(core::formula::storage_block_size(FieldKind::Flag), std::size_t(4),
              "[inference] Flag block is 4 bytes (our choice)");
    ctx.equal(core::formula::storage_block_size(FieldKind::Cost), std::size_t(8),
              "[inference] Cost block is 8 bytes (our choice)");

    ctx.check(core::formula::storage_confidence(FieldKind::Text) == StorageConfidence::Measured,
              "[measured] Text storage is measured");
    ctx.check(core::formula::storage_confidence(FieldKind::Number) == StorageConfidence::Measured,
              "[measured] Number storage is measured");
    ctx.check(core::formula::storage_confidence(FieldKind::Duration) ==
                  StorageConfidence::Measured,
              "[measured] Duration storage is measured");
    ctx.check(core::formula::storage_confidence(FieldKind::Date) == StorageConfidence::Measured,
              "[measured] Date storage is measured");
    ctx.check(core::formula::storage_confidence(FieldKind::Flag) ==
                  StorageConfidence::Unmeasured,
              "[inference] Flag storage is explicitly unmeasured");
    ctx.check(core::formula::storage_confidence(FieldKind::Cost) ==
                  StorageConfidence::Unmeasured,
              "[inference] Cost storage is explicitly unmeasured");

    const std::uint8_t four[4] = {1, 2, 3, 4};
    ctx.check(!core::formula::decode_value(FieldKind::Number, four, 4).ok,
              "[measured] a short Number block is rejected");
    ctx.check(!core::formula::decode_value(FieldKind::Duration, four, 8).ok,
              "[measured] a long Duration block is rejected");
    ctx.check(!core::formula::decode_value(FieldKind::Date, four, 8).ok,
              "[measured] a long Date block is rejected");
    ctx.check(!core::formula::decode_value(FieldKind::Flag, four, 2).ok,
              "[inference] a short Flag block is rejected");
    ctx.check(!core::formula::decode_value(FieldKind::Text, four, 0).ok,
              "[measured] an empty Text block is rejected");
    ctx.check(!core::formula::decode_value(FieldKind::Text, nullptr, 4).ok,
              "[inference] a null block pointer is rejected");
}

/* ------------------------------------------------------------------------- */
/* Registry                                                                   */
/* ------------------------------------------------------------------------- */

void test_registry(TestContext& ctx) {
    const std::vector<CustomField>& fields = core::formula::custom_fields();
    ctx.equal(fields.size(), std::size_t(100),
              "[measured] the descriptor table names 100 Task custom fields");

    std::size_t counts[6] = {0, 0, 0, 0, 0, 0};
    for (const CustomField& field : fields) {
        const std::size_t kind = static_cast<std::size_t>(field.kind);
        if (kind < 6) {
            ++counts[kind];
        }
    }
    ctx.equal(counts[0], std::size_t(30), "[measured] Text1..Text30");
    ctx.equal(counts[1], std::size_t(20), "[measured] Number1..Number20");
    ctx.equal(counts[2], std::size_t(10), "[measured] Date1..Date10");
    ctx.equal(counts[3], std::size_t(20), "[measured] Flag1..Flag20");
    ctx.equal(counts[4], std::size_t(10), "[measured] Duration1..Duration10");
    ctx.equal(counts[5], std::size_t(10), "[measured] Cost1..Cost10");

    /* The six anchors the oracle measured, in VarMeta of w10-fieldvalues.mpp. */
    const CustomField* text1 = core::formula::find_custom_field(core::formula::kTaskText1);
    ctx.check(text1 != nullptr && text1->kind == FieldKind::Text && text1->index == 1,
              "[measured] Text1 is 0x0b400033");
    ctx.check(text1 != nullptr && text1->name == "Text1",
              "[measured] 0x0b400033 is named Text1");
    const CustomField* flag1 = core::formula::find_custom_field(core::formula::kTaskFlag1);
    ctx.check(flag1 != nullptr && flag1->kind == FieldKind::Flag && flag1->index == 1,
              "[measured] Flag1 is 0x0b400048");
    const CustomField* number1 = core::formula::find_custom_field(core::formula::kTaskNumber1);
    ctx.check(number1 != nullptr && number1->kind == FieldKind::Number && number1->index == 1,
              "[measured] Number1 is 0x0b400057");
    const CustomField* duration1 =
        core::formula::find_custom_field(core::formula::kTaskDuration1);
    ctx.check(duration1 != nullptr && duration1->kind == FieldKind::Duration && duration1->index == 1,
              "[measured] Duration1 is 0x0b400067");
    const CustomField* cost1 = core::formula::find_custom_field(core::formula::kTaskCost1);
    ctx.check(cost1 != nullptr && cost1->kind == FieldKind::Cost && cost1->index == 1,
              "[measured] Cost1 is 0x0b40006a");
    const CustomField* date1 = core::formula::find_custom_field(core::formula::kTaskDate1);
    ctx.check(date1 != nullptr && date1->kind == FieldKind::Date && date1->index == 1,
              "[measured] Date1 is 0x0b400109");

    ctx.check(core::formula::find_custom_field(FieldKind::Number, 2) != nullptr &&
                  core::formula::find_custom_field(FieldKind::Number, 2)->field == 0x0b400058u,
              "[measured] Number2 is 0x0b400058");
    ctx.check(core::formula::find_custom_field(FieldKind::Number, 99) == nullptr,
              "[inference] an index past the end is not a custom field");
    ctx.check(core::formula::find_custom_field(FieldId{0}) == nullptr,
              "[inference] field id 0 is not a custom field");

    /* Cross-check every registry entry against core::model::find_field_by_id.
     * This is the test the brief asks for: the registry cannot drift from the
     * descriptor table the reader/writer use. */
    for (const CustomField& field : fields) {
        const core::model::FieldInfo* info = core::model::find_field_by_id(field.field);
        ctx.check(info != nullptr, "[measured] registry id exists in the field table");
        if (info == nullptr) {
            continue;
        }
        ctx.check(info->field_id == field.field, "[measured] the id round-trips by id");
        ctx.check(info->name == field.name, "[measured] the registry name matches the table");
        ctx.check(info->record_type == static_cast<std::uint8_t>(RecordType::Task),
                  "[measured] every custom field is a Task field");
        ctx.check(field.kind == FieldKind::Text ||
                      std::string(field.name).rfind(core::formula::field_kind_prefix(field.kind),
                                                    0) == 0,
                  "[measured] the kind prefix matches the name");
        ctx.check(core::formula::find_custom_field(field.field) == &field,
                  "[measured] find_custom_field(id) returns the same entry");
        ctx.check(core::formula::find_custom_field(field.kind, field.index) == &field,
                  "[measured] find_custom_field(kind, index) returns the same entry");
        /* Indices inside a kind are contiguous from 1. */
        ctx.check(field.index >= 1, "[measured] custom-field indices start at 1");
    }
}

/* ------------------------------------------------------------------------- */
/* Field-token resolution                                                     */
/* ------------------------------------------------------------------------- */

void test_field_resolution(TestContext& ctx) {
    ctx.check(core::formula::resolve_field_token("MSPJ188744967", RecordType::Task) ==
                  core::formula::kTaskDurationTextField,
              "[measured] MSPJ188744967 is Task.DurationText");
    ctx.check(core::formula::resolve_field_token("MSPJ188743712", RecordType::Task) ==
                  core::formula::kTaskPercentCompleteField,
              "[measured] MSPJ188743712 is Task.PercentComplete");
    ctx.check(core::formula::resolve_field_token("Duration", RecordType::Task) ==
                  core::formula::kTaskDurationField,
              "[inference] [Duration] resolves by descriptor name");
    ctx.check(core::formula::resolve_field_token("Number1", RecordType::Task) ==
                  core::formula::kTaskNumber1,
              "[measured] Number1 resolves to 0x0b400057");
    ctx.check(core::formula::resolve_field_token("number 1", RecordType::Task) ==
                  core::formula::kTaskNumber1,
              "[inference] field names ignore case and spaces");
    ctx.check(core::formula::resolve_field_token("% Complete", RecordType::Task) ==
                  core::formula::kTaskPercentCompleteField,
              "[inference] % is read as \"percent\"");
    ctx.check(core::formula::resolve_field_token("percent complete", RecordType::Task) ==
                  core::formula::kTaskPercentCompleteField,
              "[inference] the long spelling resolves too");
    ctx.check(core::formula::resolve_field_token("Flag1", RecordType::Task) ==
                  core::formula::kTaskFlag1,
              "[measured] Flag1 resolves to 0x0b400048");
    ctx.check(core::formula::resolve_field_token("Cost1", RecordType::Task) ==
                  core::formula::kTaskCost1,
              "[measured] Cost1 resolves to 0x0b40006a");
    ctx.check(core::formula::resolve_field_token("Date1", RecordType::Task) ==
                  core::formula::kTaskDate1,
              "[measured] Date1 resolves to 0x0b400109");
    ctx.check(core::formula::resolve_field_token("NoSuchField", RecordType::Task) == 0,
              "[inference] an unknown name resolves to 0");
    ctx.check(core::formula::resolve_field_token("MSPJ", RecordType::Task) == 0,
              "[inference] MSPJ without digits is not a field");
    ctx.check(core::formula::resolve_field_token("MSPJ0", RecordType::Task) == 0,
              "[inference] field id 0 is not a field");
    ctx.check(core::formula::resolve_field_token("MSPJabc", RecordType::Task) == 0,
              "[inference] MSPJ with non-digits is not a field");
    ctx.check(core::formula::resolve_field_token("MSPJ999999", RecordType::Task) == 0,
              "[inference] an id outside the table is not a field");

    FieldId id = 0;
    ctx.check(core::formula::field_id_from_mspj("MSPJ188744967", id) && id == 0x0b400507u,
              "[measured] field_id_from_mspj reads the decimal token");
    ctx.check(core::formula::field_id_from_mspj("mspj188744967", id),
              "[inference] the MSPJ prefix is case-insensitive");
    ctx.check(!core::formula::field_id_from_mspj("188744967", id),
              "[inference] a bare decimal id is not the stored token form");
}

/* ------------------------------------------------------------------------- */
/* Computed columns                                                           */
/* ------------------------------------------------------------------------- */

void test_computed_columns(TestContext& ctx) {
    RecordSet records = make_tasks();
    ComputedColumns columns;
    std::string error;

    ctx.check(columns.add(core::formula::kTaskNumber1, "Double", "[Duration]*2", error),
              "[inference] a computed column parses and registers");
    ctx.check(error.empty(), "[inference] a successful add leaves no error");
    ctx.equal(columns.size(), std::size_t(1), "[inference] one computed column");
    ctx.equal(columns.column(0).name, std::string("Double"), "[inference] the name is stored");
    ctx.equal(columns.column(0).formula, std::string("[Duration]*2"),
              "[inference] the source formula is stored");
    ctx.check(columns.expression(0) != nullptr, "[inference] the parsed AST is exposed");
    ctx.equal(columns.index_of(core::formula::kTaskNumber1), 0,
              "[inference] index_of finds the column");
    ctx.equal(columns.index_of(core::formula::kTaskText1), -1,
              "[inference] index_of misses an uncomputed field");

    /* The accessor overrides the stored value before evaluate() runs. */
    ctx.check(is_duration(columns.value_of_field(records, 0, core::formula::kTaskNumber1),
                          9600),
              "[inference] value_of_field evaluates the formula");
    const Value* stored = records.get(0, core::formula::kTaskNumber1);
    ctx.check(stored != nullptr && is_number(*stored, 42.5),
              "[inference] the RecordSet still holds the raw value before evaluate");
    ctx.check(is_text(columns.value_of_field(records, 0, task_field("Name")), "Design"),
              "[inference] value_of_field falls back to the RecordSet for base fields");

    ctx.equal(columns.evaluate(records), std::size_t(3),
              "[inference] evaluate writes one value per row");
    for (std::size_t row = 0; row < records.row_count(); ++row) {
        const Value computed =
            columns.value_of_field(records, row, core::formula::kTaskNumber1);
        const Value* written = records.get(row, core::formula::kTaskNumber1);
        ctx.check(written != nullptr && computed.kind == written->kind &&
                      computed.integer == written->integer,
                  "[inference] the sheet value and the accessor agree after evaluate");
    }
    ctx.check(is_duration(columns.value_of_field(records, 0, core::formula::kTaskNumber1),
                          9600),
              "[inference] the computed value survives evaluate");

    ctx.check(columns.add(core::formula::kTaskFlag1, "Done",
                          "IIf([PercentComplete]>50,True,False)", error),
              "[inference] a Boolean computed column registers");
    ctx.equal(columns.evaluate(records), std::size_t(6),
              "[inference] evaluate writes every computed column for every row");
    ctx.check(is_bool(columns.value_of_field(records, 0, core::formula::kTaskFlag1), false),
              "[inference] 25% is not done");
    ctx.check(is_bool(columns.value_of_field(records, 1, core::formula::kTaskFlag1), true),
              "[inference] 75% is done");
    ctx.check(is_bool(columns.value_of_field(records, 2, core::formula::kTaskFlag1), false),
              "[inference] 50% is not > 50");

    /* Chained computed columns: Number1 is itself computed. */
    ctx.check(columns.add(core::formula::kTaskCost1, "Quad", "[Number1]*2", error),
              "[inference] a formula may reference another computed field");
    ctx.check(is_duration(columns.value_of_field(records, 0, core::formula::kTaskCost1),
                          19200),
              "[inference] the chain evaluates through Number1");

    /* Errors. */
    ctx.check(!columns.add(core::formula::kTaskNumber1, "Again", "1", error),
              "[inference] a duplicate computed field is rejected");
    ctx.contains(error, "already", "[inference] the duplicate message");
    ctx.check(!columns.add(FieldId{0}, "Zero", "1", error),
              "[inference] field id 0 is rejected");
    ctx.check(!columns.add(0xdeadbeefu, "Bogus", "1", error),
              "[inference] an unknown field id is rejected");
    ctx.contains(error, "unknown field id", "[inference] the unknown-id message");
    ctx.check(!columns.add(custom_number(2), "Bad", "[Nope]", error),
              "[inference] a formula error is rejected");
    ctx.contains(error, "offset 0", "[inference] the parser offset reaches the caller");

    /* A cycle must terminate through the depth guard, not recurse forever. */
    ComputedColumns cyclic;
    ctx.check(cyclic.add(custom_number(3), "A", "[Number4]+1", error),
              "[inference] the first half of a cycle registers");
    ctx.check(cyclic.add(custom_number(4), "B", "[Number3]+1", error),
              "[inference] the second half of a cycle registers");
    ctx.check(is_null(cyclic.value_of_field(records, 0, custom_number(3))),
              "[inference] a formula cycle terminates as Null");
    cyclic.set_max_depth(1);
    ctx.equal(cyclic.max_depth(), 1, "[inference] the depth guard is configurable");
    ctx.check(is_null(cyclic.value_of_field(records, 0, custom_number(3))),
              "[inference] a shallow depth guard still terminates");
}

void test_sheet_integration(TestContext& ctx) {
    RecordSet records = make_tasks();

    std::vector<core::sheet::SheetColumn> sheet_columns;
    core::sheet::SheetColumn name_column;
    name_column.field = task_field("Name");
    name_column.title = "Task Name";
    sheet_columns.push_back(name_column);

    core::sheet::SheetModel sheet(records, std::move(sheet_columns));
    ComputedColumns columns;
    std::string error;

    ctx.equal(sheet.column_count(), std::size_t(1), "[inference] the sheet starts with one column");
    ctx.check(columns.add_to_sheet(sheet, core::formula::kTaskNumber1, "Double",
                                   "[Duration]*2", error),
              "[inference] add_to_sheet succeeds through the public API");
    ctx.equal(sheet.column_count(), std::size_t(2), "[inference] the computed column was appended");
    ctx.equal(sheet.column_of_field(core::formula::kTaskNumber1), std::size_t(1),
              "[inference] the new column carries the computed field");
    ctx.equal(sheet.column(1).title, std::string("Double"),
              "[inference] the authored title is the header");
    ctx.equal(sheet.column_title(1), std::string("Double"),
              "[inference] column_title reports it");

    const Value* cell = sheet.cell_value(0, 1);
    ctx.check(cell != nullptr && is_duration(*cell, 9600),
              "[inference] the sheet cell holds the computed duration");
    ctx.check(cell != nullptr &&
                  is_duration(columns.value_of_field(sheet.records(), 0,
                                                     core::formula::kTaskNumber1),
                              9600),
              "[inference] a report calling value_of_field agrees with the sheet cell");
    for (std::size_t row = 0; row < sheet.row_count(); ++row) {
        const Value* sheet_cell = sheet.cell_value(row, 1);
        const Value computed =
            columns.value_of_field(sheet.records(), row, core::formula::kTaskNumber1);
        ctx.check(sheet_cell != nullptr && sheet_cell->kind == computed.kind &&
                      sheet_cell->integer == computed.integer,
                  "[inference] every sheet row equals the single accessor");
    }

    /* Re-adding the same field must not duplicate the column. */
    ctx.check(columns.add_to_sheet(sheet, core::formula::kTaskNumber1, "Double again",
                                   "[Duration]*3", error),
              "[inference] adding the same field again succeeds");
    ctx.equal(sheet.column_count(), std::size_t(2),
              "[inference] no duplicate column was appended");
    ctx.check(columns.value_of_field(sheet.records(), 0, core::formula::kTaskNumber1).kind ==
                  ValueKind::Duration,
              "[inference] the accessor still computes the field");
}

}  // namespace

int main() {
    TestContext ctx;

    test_tokenizer(ctx);
    test_parser(ctx);
    test_parse_errors(ctx);
    test_nesting_limits(ctx);
    test_arithmetic(ctx);
    test_dates_and_durations(ctx);
    test_encodings(ctx);
    test_registry(ctx);
    test_field_resolution(ctx);
    test_computed_columns(ctx);
    test_sheet_integration(ctx);

    return ctx.finish("formula_tests");
}
