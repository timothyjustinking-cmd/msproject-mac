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

    static FilterCriterion comparison(FieldId field, FilterOp op, Value value);
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
