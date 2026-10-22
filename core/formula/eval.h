/*
 * eval.h -- evaluating a parsed formula against one row of field values.
 *
 * SEMANTICS ARE INFERENCE, NOT MEASUREMENT
 *   The wave-10 oracle measured the *storage* of custom fields and the
 *   [MSPJ<id>] tokens a formula definition is stored as, but not one evaluated
 *   result: Project does not compute a custom field into a headless GetField
 *   (w10-oracle.md section 2).  Every rule below follows Project's documented
 *   formula language, and each one is tagged inference in docs/handoff/w44.md.
 *   The rules that matter most:
 *
 *   Value kinds  exactly core::view's seven.  A field's value comes from ONE
 *                accessor, base_value_of_field()/ComputedColumns::value_of_field(),
 *                so the sheet and a report cannot disagree.
 *
 *   [Duration]   The stored token is [MSPJ188744967] = Task.DurationText.  That
 *                field evaluates to the task's Duration (ticks), not to text:
 *                a DurationText reference falls back to Task.Duration when the
 *                row has no DurationText value.  This is why "[MSPJ188744967]*2"
 *                is duration arithmetic.  Inference; the token id is measured.
 *
 *   Arithmetic   Integer/Number/Bool/Text-that-parses are numeric.  A Duration
 *                plus/minus a number treats the number as DAYS (4800 ticks);
 *                Duration * number and Duration / number stay Duration;
 *                Duration / Duration is a plain Number ratio.  A Date plus an
 *                integer is that many days later (fractional days truncate
 *                toward zero); Date - Date is a Duration; Date * or / is Null.
 *                Division by zero is Null, not an error value.
 *
 *   Coercion     comparisons use core::view::compare_values, i.e. the same
 *                coercion sort/filter use: Text "8" compares numerically with
 *                Integer 8, and kinds fall back to the view rank order.  So a
 *                formula and a filter over the same values agree.
 *
 *   Null         Null propagates through arithmetic, comparison, concat and
 *                the date/duration functions: the result is Null ("blank").
 *                It is not an error and it is not 0.  `&` treats Null as "",
 *                IIf treats a Null condition as false, and IsNull() is the
 *                explicit test.  (Project has an error value #ERROR; the view
 *                Value has none, so a type mismatch is Null here -- inference.)
 *
 *   Text         the '&' operator renders each side with core::sheet's
 *                display formatting ("1 day", "Tue 20/10/26", "Yes"), because
 *                that is the text Project's own formula UI would show.
 *
 *   Truth        Bool itself; Null false; a number/duration is true when
 *                non-zero; a date is true when valid; Text is true when
 *                non-empty and not "0"/"no"/"false" (case-insensitive).
 *
 * FUNCTIONS (all inference; the names are Project's documented set)
 *   IIf(condition, true-part, false-part)   lazy: only the chosen branch runs
 *   ProjDurValue(duration)                  number of minutes (ticks / 10)
 *   ProjDurConv(duration, unit)             number in "d"/"h"/"m"/"w"/"mo"/"s"
 *   ProjDateAdd(date, duration)             date shifted forward
 *   ProjDateSub(date, duration)             date shifted back
 *   ProjDateDiff(date, date)                Duration difference
 *   DateValue(text)                         parses ISO yyyy-mm-dd[Thh:mm[:ss]]
 *   Day/Month/Year/Hour/Minute/Weekday(date) integers; Weekday 1 = Sunday
 *   IsNull(value)                           Bool
 */

#ifndef CORE_FORMULA_EVAL_H
#define CORE_FORMULA_EVAL_H

#include <cstddef>
#include <functional>
#include <string>

#include "expr.h"
#include "record_set.h"
#include "sheet.h"
#include "value.h"

namespace core::formula {

using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;

/*
 * Everything one evaluation needs.  `records` may be null when every field
 * reference is served by `resolve`; `row` indexes the RecordSet.  `depth` is a
 * computed-column recursion guard owned by the caller that builds `resolve`.
 */
struct EvalContext {
    const RecordSet* records = nullptr;
    std::size_t row = 0;
    core::model::RecordType record_type = core::model::RecordType::Task;
    core::sheet::SheetFormat format{};
    std::function<Value(FieldId)> resolve;
    int depth = 0;
    int max_depth = 32;
};

struct EvalResult {
    bool ok = true;
    Value value{};
    std::string error;
};

/*
 * The base (non-computed) value of a field for one row: the RecordSet value,
 * else Null.  A DurationText reference falls back to Task.Duration (see the
 * header comment).  This is the single row read the computed-column layer sits
 * on top of.
 */
Value base_value_of_field(const RecordSet& records, std::size_t row, FieldId field) noexcept;

/* Evaluates one AST node.  Never throws; an error is ok=false with a message.
 *
 * The evaluator enforces the same nesting bound as the parser, kMaxFormulaDepth
 * (expr.h).  An AST deeper than that -- only reachable from a hand-built tree,
 * since parse() refuses one -- is reported as ok=false with an
 * "exceeds ... levels" error rather than recursing into a stack overflow. */
EvalResult evaluate(const Expr& expr, const EvalContext& context);

bool truthy(const Value& value) noexcept;

/* The display text '&' and the text functions use. */
std::string to_text(const Value& value, const core::sheet::SheetFormat& format);

}  // namespace core::formula

#endif  // CORE_FORMULA_EVAL_H
