#include "filter.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace core::view {
namespace {

JsonValue value_to_json(const Value& value) {
    switch (value.kind) {
        case ValueKind::Null: return JsonValue::null();
        case ValueKind::Bool: return JsonValue::boolean(value.boolean);
        case ValueKind::Integer: return JsonValue::integer(value.integer);
        case ValueKind::Number: return JsonValue::number(value.number);
        case ValueKind::Duration: return JsonValue::string(format_duration(value.integer));
        case ValueKind::Date: return JsonValue::string(format_date(value.date));
        case ValueKind::Text: return JsonValue::string(value.text);
    }
    return JsonValue::null();
}

bool value_from_json(const JsonValue& json, Value& value, std::string_view path,
                     std::string& error) {
    switch (json.type()) {
        case JsonValue::Type::Null: value = Value{}; return true;
        case JsonValue::Type::Bool: value = Value::of_bool(json.as_bool()); return true;
        case JsonValue::Type::Number: {
            const double raw = json.as_number();
            if (raw == static_cast<double>(static_cast<std::int64_t>(raw))) {
                value = Value::of_int(static_cast<std::int64_t>(raw));
            } else {
                value = Value::of_number(raw);
            }
            return true;
        }
        case JsonValue::Type::String: value = Value::of_text(json.as_string()); return true;
        case JsonValue::Type::Array:
        case JsonValue::Type::Object:
            error = std::string(path) + ": filter constants are scalars";
            return false;
    }
    error = std::string(path) + ": invalid filter constant";
    return false;
}

std::string display_text(const Value& value) {
    if (value.kind == ValueKind::Text) {
        return value.text;
    }
    return format_value(value);
}

bool text_op(FilterOp op, const std::string& haystack, const std::string& needle) {
    switch (op) {
        case FilterOp::Contains: return haystack.find(needle) != std::string::npos;
        case FilterOp::NotContains: return haystack.find(needle) == std::string::npos;
        case FilterOp::StartsWith:
            return haystack.size() >= needle.size() &&
                   haystack.compare(0, needle.size(), needle) == 0;
        case FilterOp::EndsWith:
            return haystack.size() >= needle.size() &&
                   haystack.compare(haystack.size() - needle.size(), needle.size(), needle) == 0;
        default: return false;
    }
}

bool evaluate_comparison(const FilterCriterion& criterion, const RecordSet& records,
                         std::size_t row) {
    const Value* pointer = records.get(row, criterion.field);
    const Value actual = pointer != nullptr ? *pointer : Value{};
    Value constant = criterion.value;
    if (criterion.op == FilterOp::IsNull) {
        return actual.is_null();
    }
    if (criterion.op == FilterOp::IsNotNull) {
        return !actual.is_null();
    }
    if (criterion.op == FilterOp::In || criterion.op == FilterOp::NotIn) {
        const bool found = value_in_list(actual, criterion.values);
        return criterion.op == FilterOp::In ? found : !found;
    }
    if (criterion.op == FilterOp::IsTrue || criterion.op == FilterOp::IsFalse) {
        const Value as_bool = coerce_value(actual, ValueKind::Bool);
        if (as_bool.is_null()) {
            return false;
        }
        return criterion.op == FilterOp::IsTrue ? as_bool.boolean : !as_bool.boolean;
    }
    if (criterion.op == FilterOp::Contains || criterion.op == FilterOp::NotContains ||
        criterion.op == FilterOp::StartsWith || criterion.op == FilterOp::EndsWith) {
        return text_op(criterion.op, display_text(actual), display_text(constant));
    }
    if (actual.is_null() || constant.is_null()) {
        const bool both_null = actual.is_null() && constant.is_null();
        if (criterion.op == FilterOp::Equals) {
            return both_null;
        }
        if (criterion.op == FilterOp::NotEquals) {
            return !both_null;
        }
        return false;
    }
    if (constant.kind != actual.kind) {
        const Value coerced = coerce_value(constant, actual.kind);
        if (!coerced.is_null()) {
            constant = coerced;
        }
    }
    const int order = compare_values(actual, constant);
    switch (criterion.op) {
        case FilterOp::Equals: return order == 0;
        case FilterOp::NotEquals: return order != 0;
        case FilterOp::LessThan: return order < 0;
        case FilterOp::LessOrEqual: return order <= 0;
        case FilterOp::GreaterThan: return order > 0;
        case FilterOp::GreaterOrEqual: return order >= 0;
        default: return false;
    }
}

}  // namespace

const char* filter_op_name(FilterOp op) noexcept {
    switch (op) {
        case FilterOp::Equals: return "equals";
        case FilterOp::NotEquals: return "not_equals";
        case FilterOp::LessThan: return "less_than";
        case FilterOp::LessOrEqual: return "less_or_equal";
        case FilterOp::GreaterThan: return "greater_than";
        case FilterOp::GreaterOrEqual: return "greater_or_equal";
        case FilterOp::Contains: return "contains";
        case FilterOp::NotContains: return "not_contains";
        case FilterOp::StartsWith: return "starts_with";
        case FilterOp::EndsWith: return "ends_with";
        case FilterOp::IsTrue: return "is_true";
        case FilterOp::IsFalse: return "is_false";
        case FilterOp::IsNull: return "is_null";
        case FilterOp::IsNotNull: return "is_not_null";
        case FilterOp::In: return "in";
        case FilterOp::NotIn: return "not_in";
    }
    return "equals";
}

bool filter_op_from_name(std::string_view name, FilterOp& out) noexcept {
    if (name == "equals") { out = FilterOp::Equals; return true; }
    if (name == "not_equals") { out = FilterOp::NotEquals; return true; }
    if (name == "less_than") { out = FilterOp::LessThan; return true; }
    if (name == "less_or_equal") { out = FilterOp::LessOrEqual; return true; }
    if (name == "greater_than") { out = FilterOp::GreaterThan; return true; }
    if (name == "greater_or_equal") { out = FilterOp::GreaterOrEqual; return true; }
    if (name == "contains") { out = FilterOp::Contains; return true; }
    if (name == "not_contains") { out = FilterOp::NotContains; return true; }
    if (name == "starts_with") { out = FilterOp::StartsWith; return true; }
    if (name == "ends_with") { out = FilterOp::EndsWith; return true; }
    if (name == "is_true") { out = FilterOp::IsTrue; return true; }
    if (name == "is_false") { out = FilterOp::IsFalse; return true; }
    if (name == "is_null") { out = FilterOp::IsNull; return true; }
    if (name == "is_not_null") { out = FilterOp::IsNotNull; return true; }
    if (name == "in") { out = FilterOp::In; return true; }
    if (name == "not_in") { out = FilterOp::NotIn; return true; }
    return false;
}

FilterCriterion FilterCriterion::comparison(FieldId field, FilterOp op, Value value) {
    FilterCriterion criterion;
    criterion.kind = Kind::Comparison;
    criterion.field = field;
    criterion.op = op;
    criterion.value = std::move(value);
    return criterion;
}

FilterCriterion FilterCriterion::in_list(FieldId field, std::vector<Value> values) {
    FilterCriterion criterion;
    criterion.kind = Kind::Comparison;
    criterion.field = field;
    criterion.op = FilterOp::In;
    criterion.values = std::move(values);
    return criterion;
}

FilterCriterion FilterCriterion::not_in_list(FieldId field, std::vector<Value> values) {
    FilterCriterion criterion;
    criterion.kind = Kind::Comparison;
    criterion.field = field;
    criterion.op = FilterOp::NotIn;
    criterion.values = std::move(values);
    return criterion;
}

FilterCriterion FilterCriterion::is_null(FieldId field) {
    return comparison(field, FilterOp::IsNull, Value{});
}

FilterCriterion FilterCriterion::is_not_null(FieldId field) {
    return comparison(field, FilterOp::IsNotNull, Value{});
}

FilterCriterion FilterCriterion::all(std::vector<FilterCriterion> children) {
    FilterCriterion criterion;
    criterion.kind = Kind::All;
    criterion.children = std::move(children);
    return criterion;
}

FilterCriterion FilterCriterion::any(std::vector<FilterCriterion> children) {
    FilterCriterion criterion;
    criterion.kind = Kind::Any;
    criterion.children = std::move(children);
    return criterion;
}

FilterCriterion FilterCriterion::negate(FilterCriterion child) {
    FilterCriterion criterion;
    criterion.kind = Kind::Not;
    criterion.children.push_back(std::move(child));
    return criterion;
}

bool matches(const FilterCriterion& criterion, const RecordSet& records, std::size_t row) {
    if (row >= records.row_count()) {
        return false;
    }
    switch (criterion.kind) {
        case FilterCriterion::Kind::Comparison:
            return evaluate_comparison(criterion, records, row);
        case FilterCriterion::Kind::All:
            for (const auto& child : criterion.children) {
                if (!matches(child, records, row)) {
                    return false;
                }
            }
            return true;
        case FilterCriterion::Kind::Any:
            for (const auto& child : criterion.children) {
                if (matches(child, records, row)) {
                    return true;
                }
            }
            return false;
        case FilterCriterion::Kind::Not:
            return criterion.children.empty() ? false : !matches(criterion.children[0], records, row);
    }
    return false;
}

std::vector<std::size_t> matching_rows(const FilterDefinition& filter,
                                       const RecordSet& records) {
    std::vector<std::size_t> rows;
    rows.reserve(records.row_count());
    for (std::size_t row = 0; row < records.row_count(); ++row) {
        if (matches(filter.criteria, records, row)) {
            rows.push_back(row);
        }
    }
    return rows;
}

bool value_in_list(const Value& value, const std::vector<Value>& values) {
    for (const Value& candidate : values) {
        if (value.is_null() || candidate.is_null()) {
            if (value.is_null() && candidate.is_null()) {
                return true;
            }
            continue;
        }
        Value constant = candidate;
        if (constant.kind != value.kind) {
            const Value coerced = coerce_value(constant, value.kind);
            if (!coerced.is_null()) {
                constant = coerced;
            }
        }
        if (compare_values(value, constant) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<Value> distinct_values(const RecordSet& records, FieldId field) {
    std::vector<Value> distinct;
    if (!records.has_field(field)) {
        return distinct;
    }
    for (std::size_t row = 0; row < records.row_count(); ++row) {
        const Value* pointer = records.get(row, field);
        if (pointer == nullptr || pointer->is_null()) {
            continue;
        }
        bool seen = false;
        for (const Value& existing : distinct) {
            if (existing.kind == pointer->kind && compare_values(existing, *pointer) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            distinct.push_back(*pointer);
        }
    }
    std::stable_sort(distinct.begin(), distinct.end(), [](const Value& left, const Value& right) {
        return compare_values(left, right) < 0;
    });
    return distinct;
}

FilterCriterion auto_filter_criterion(FieldId field, std::vector<Value> values) {
    return FilterCriterion::in_list(field, std::move(values));
}

FilterDefinition auto_filter(std::string id, std::string name,
                             core::model::RecordType record_type, FieldId field,
                             std::vector<Value> values) {
    FilterDefinition filter;
    filter.id = std::move(id);
    filter.name = std::move(name);
    filter.record_type = record_type;
    filter.criteria = auto_filter_criterion(field, std::move(values));
    return filter;
}

JsonValue criterion_to_json(const FilterCriterion& criterion, core::model::RecordType record_type) {
    JsonValue json = JsonValue::object();
    switch (criterion.kind) {
        case FilterCriterion::Kind::Comparison: {
            const JsonValue reference = field_ref_to_json(criterion.field, record_type);
            for (const auto& member : reference.members()) {
                json.set(member.first, member.second);
            }
            json.set("op", JsonValue::string(filter_op_name(criterion.op)));
            if (criterion.op == FilterOp::In || criterion.op == FilterOp::NotIn) {
                JsonValue values = JsonValue::array();
                for (const Value& value : criterion.values) {
                    values.push(value_to_json(value));
                }
                json.set("values", std::move(values));
            } else if (criterion.op != FilterOp::IsTrue &&
                       criterion.op != FilterOp::IsFalse &&
                       criterion.op != FilterOp::IsNull &&
                       criterion.op != FilterOp::IsNotNull) {
                json.set("value", value_to_json(criterion.value));
            }
            break;
        }
        case FilterCriterion::Kind::All:
        case FilterCriterion::Kind::Any: {
            JsonValue children = JsonValue::array();
            for (const auto& child : criterion.children) {
                children.push(criterion_to_json(child, record_type));
            }
            json.set(criterion.kind == FilterCriterion::Kind::All ? "and" : "or",
                     std::move(children));
            break;
        }
        case FilterCriterion::Kind::Not:
            if (!criterion.children.empty()) {
                json.set("not", criterion_to_json(criterion.children[0], record_type));
            }
            break;
    }
    return json;
}

bool criterion_from_json(const JsonValue& json, core::model::RecordType record_type,
                         FilterCriterion& criterion, std::string_view path,
                         std::string& error) {
    const std::string prefix = path.empty() ? std::string() : std::string(path) + ": ";
    if (!json.is_object()) {
        error = prefix + "criterion must be an object";
        return false;
    }
    const JsonValue* field = json.find("field");
    const JsonValue* field_id = json.find("field_id");
    const JsonValue* op = json.find("op");
    const JsonValue* all = json.find("and");
    const JsonValue* any = json.find("or");
    const JsonValue* not_value = json.find("not");
    int kinds = 0;
    if (field != nullptr || field_id != nullptr) ++kinds;
    if (all != nullptr) ++kinds;
    if (any != nullptr) ++kinds;
    if (not_value != nullptr) ++kinds;
    if (kinds != 1) {
        error = prefix + "criterion needs exactly one of a field comparison, \"and\", "
                "\"or\" or \"not\"";
        return false;
    }
    if (all != nullptr || any != nullptr) {
        const JsonValue& children_json = all != nullptr ? *all : *any;
        if (!children_json.is_array() || children_json.items().empty()) {
            error = prefix + "boolean criterion needs a non-empty array";
            return false;
        }
        std::vector<FilterCriterion> children;
        children.reserve(children_json.items().size());
        for (std::size_t i = 0; i < children_json.items().size(); ++i) {
            FilterCriterion child;
            if (!criterion_from_json(children_json.items()[i], record_type, child,
                                     prefix + (all != nullptr ? "and" : "or") + "[" +
                                         std::to_string(i) + "]",
                                     error)) {
                return false;
            }
            children.push_back(std::move(child));
        }
        criterion = all != nullptr ? FilterCriterion::all(std::move(children))
                                   : FilterCriterion::any(std::move(children));
        return true;
    }
    if (not_value != nullptr) {
        FilterCriterion child;
        if (!criterion_from_json(*not_value, record_type, child, prefix + "not", error)) {
            return false;
        }
        criterion = FilterCriterion::negate(std::move(child));
        return true;
    }
    if (op == nullptr || !op->is_string()) {
        error = prefix + "comparison needs an \"op\" string";
        return false;
    }
    FilterOp parsed_op = FilterOp::Equals;
    if (!filter_op_from_name(op->as_string(), parsed_op)) {
        error = prefix + "unknown filter op \"" + op->as_string() + "\"";
        return false;
    }
    FieldId parsed_field = 0;
    if (!field_ref_from_json(json, record_type, parsed_field, prefix, error)) {
        return false;
    }
    if (parsed_op == FilterOp::In || parsed_op == FilterOp::NotIn) {
        const JsonValue* values_json = json.find("values");
        if (values_json == nullptr) {
            const JsonValue* value_json = json.find("value");
            if (value_json != nullptr && value_json->is_array()) {
                values_json = value_json;
            }
        }
        if (values_json == nullptr || !values_json->is_array()) {
            error = prefix + "in/not_in needs a \"values\" array";
            return false;
        }
        std::vector<Value> values;
        values.reserve(values_json->items().size());
        for (std::size_t i = 0; i < values_json->items().size(); ++i) {
            Value member;
            if (!value_from_json(values_json->items()[i], member,
                                 prefix + "values[" + std::to_string(i) + "]", error)) {
                return false;
            }
            values.push_back(std::move(member));
        }
        criterion = parsed_op == FilterOp::In
                        ? FilterCriterion::in_list(parsed_field, std::move(values))
                        : FilterCriterion::not_in_list(parsed_field, std::move(values));
        return true;
    }
    if (parsed_op == FilterOp::IsNull || parsed_op == FilterOp::IsNotNull) {
        criterion = FilterCriterion::comparison(parsed_field, parsed_op, Value{});
        return true;
    }
    Value value;
    if (parsed_op != FilterOp::IsTrue && parsed_op != FilterOp::IsFalse) {
        const JsonValue* value_json = json.find("value");
        if (value_json == nullptr) {
            error = prefix + "comparison needs a \"value\"";
            return false;
        }
        if (!value_from_json(*value_json, value, prefix + "value", error)) {
            return false;
        }
    }
    criterion = FilterCriterion::comparison(parsed_field, parsed_op, std::move(value));
    return true;
}

JsonValue filter_to_json(const FilterDefinition& filter) {
    JsonValue json = JsonValue::object();
    json.set("id", JsonValue::string(filter.id));
    json.set("name", JsonValue::string(filter.name));
    json.set("record_type",
             JsonValue::string(core::model::record_type_name(filter.record_type)));
    json.set("criteria", criterion_to_json(filter.criteria, filter.record_type));
    return json;
}

bool filter_from_json(const JsonValue& json, FilterDefinition& filter, std::string_view path,
                      std::string& error) {
    const std::string prefix = path.empty() ? std::string() : std::string(path) + ": ";
    if (!json.is_object()) {
        error = prefix + "filter must be an object";
        return false;
    }
    const JsonValue* id = json.find("id");
    const JsonValue* name = json.find("name");
    const JsonValue* criteria = json.find("criteria");
    if (id == nullptr || !id->is_string() || id->as_string().empty()) {
        error = prefix + "filter needs a non-empty \"id\"";
        return false;
    }
    if (name == nullptr || !name->is_string()) {
        error = prefix + "filter needs a \"name\" string";
        return false;
    }
    if (criteria == nullptr) {
        error = prefix + "filter needs \"criteria\"";
        return false;
    }
    FilterDefinition parsed;
    parsed.id = id->as_string();
    parsed.name = name->as_string();
    if (const JsonValue* record_type = json.find("record_type")) {
        if (!record_type->is_string() ||
            !record_type_from_name(record_type->as_string(), parsed.record_type)) {
            error = prefix + "unknown \"record_type\"";
            return false;
        }
    }
    if (!criterion_from_json(*criteria, parsed.record_type, parsed.criteria,
                             prefix + "criteria", error)) {
        return false;
    }
    filter = std::move(parsed);
    return true;
}

}  // namespace core::view
