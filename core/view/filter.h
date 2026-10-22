/*
 * filter.h -- filter definitions as a criteria tree over field ids.
 *
 * MS Project filters are a tree of comparisons combined with All/Any/Not, plus
 * a few field predicates.  The tree is plain data so it can be described in a
 * test, serialised, and evaluated against any RecordSet.  There is no UI here
 * and no opinion about where the filter is shown.
 *
 * OPERATOR SEMANTICS (implementation choices where no oracle evidence exists):
 *   * comparisons are case-sensitive and uses the field's own kind;
 *   * a filter constant is coerced to the field's kind when possible
 *     (Text "PT8H0M0S" against a Duration field, Text "2024-01-01" against a
 *     Date field).  Write constants as JSON strings for those kinds.
 *   * a Null field value equals a Null constant and nothing else; ordering
 *     comparisons against Null are false.
 *   * contains/starts_with/ends_with operate on the value's display text and
 *     are case-sensitive.
 *   * is_null / is_not_null test the field value itself; their constant is
 *     ignored.
 *   * in / not_in carry a value list (FilterCriterion::values); membership uses
 *     the same equality and coercion as Equals, so a null field value only
 *     matches a null member.
 */

#ifndef CORE_VIEW_FILTER_H
#define CORE_VIEW_FILTER_H

#include <string>
#include <string_view>
#include <vector>

#include "field_ref.h"
#include "json.h"
#include "record_set.h"
#include "value.h"

namespace core::view {

enum class FilterOp : std::uint8_t {
    Equals = 0,
    NotEquals,
    LessThan,
    LessOrEqual,
    GreaterThan,
    GreaterOrEqual,
    Contains,
    NotContains,
    StartsWith,
    EndsWith,
    IsTrue,
    IsFalse,
    // Appended so the existing numeric values (and JSON round-trips written by
    // older builds) keep their meaning.
    IsNull,
    IsNotNull,
    In,     // value list: any member equals the field value
    NotIn,  // negation of In
};

const char* filter_op_name(FilterOp op) noexcept;
bool filter_op_from_name(std::string_view name, FilterOp& out) noexcept;

struct FilterCriterion {
    enum class Kind : std::uint8_t { Comparison = 0, All, Any, Not };

    Kind kind = Kind::Comparison;
    FieldId field = 0;      // Comparison
    FilterOp op = FilterOp::Equals;
    Value value;            // Comparison constant; may be Null
    std::vector<FilterCriterion> children;  // All/Any (1..n), Not (exactly 1)
    // In/NotIn constants, in authored order.  Appended so the aggregate layout
    // above is unchanged.
    std::vector<Value> values;

    static FilterCriterion comparison(FieldId field, FilterOp op, Value value);
    static FilterCriterion in_list(FieldId field, std::vector<Value> values);
    static FilterCriterion not_in_list(FieldId field, std::vector<Value> values);
    static FilterCriterion is_null(FieldId field);
    static FilterCriterion is_not_null(FieldId field);
    static FilterCriterion all(std::vector<FilterCriterion> children);
    static FilterCriterion any(std::vector<FilterCriterion> children);
    static FilterCriterion negate(FilterCriterion child);
};

struct FilterDefinition {
    std::string id;
    std::string name;
    core::model::RecordType record_type = core::model::RecordType::Task;
    FilterCriterion criteria;
};

/* True when the row passes.  A Null-valued field matches only Null constants. */
bool matches(const FilterCriterion& criterion, const RecordSet& records, std::size_t row);

/* Row indices in record order that pass. */
std::vector<std::size_t> matching_rows(const FilterDefinition& filter,
                                       const RecordSet& records);

/*
 * Auto-filter support.  MS Project's AutoFilter drop-down lists the distinct
 * values in a column, so this returns one value per distinct value, in
 * ascending value order (the same compare_values order sort uses).  Null and
 * absent values are not listed; a text value is kept verbatim (the display
 * form is the caller's job).  An absent field yields an empty list.
 */
std::vector<Value> distinct_values(const RecordSet& records, FieldId field);

/* True when `value` is one of `values`, with the same coercion rules as the
 * Equals comparison. */
bool value_in_list(const Value& value, const std::vector<Value>& values);

/* A criterion for the auto-filter: "field is any of these values". */
FilterCriterion auto_filter_criterion(FieldId field, std::vector<Value> values);

/* The whole definition, ready for matching_rows(). */
FilterDefinition auto_filter(std::string id, std::string name,
                             core::model::RecordType record_type, FieldId field,
                             std::vector<Value> values);

JsonValue filter_to_json(const FilterDefinition& filter);
bool filter_from_json(const JsonValue& json, FilterDefinition& filter, std::string_view path,
                      std::string& error);

JsonValue criterion_to_json(const FilterCriterion& criterion,
                            core::model::RecordType record_type = core::model::RecordType::Task);
bool criterion_from_json(const JsonValue& json, core::model::RecordType record_type,
                         FilterCriterion& criterion, std::string_view path,
                         std::string& error);

}  // namespace core::view

#endif  // CORE_VIEW_FILTER_H
