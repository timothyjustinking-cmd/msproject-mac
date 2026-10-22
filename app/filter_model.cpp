// app/filter_model.cpp -- see filter_model.h for what a filter does (and does
// not) do to the model.

#include "filter_model.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "value.h"

namespace app {
namespace {

using core::view::FilterCriterion;
using core::view::FilterOp;
using core::view::Value;

std::string quoted(std::string_view text) {
    std::string out = "\"";
    out += text;
    out += "\"";
    return out;
}

std::string_view trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool op_takes_value(FilterOp op) noexcept {
    switch (op) {
        case FilterOp::IsNull:
        case FilterOp::IsNotNull:
        case FilterOp::IsTrue:
        case FilterOp::IsFalse:
            return false;
        default:
            return true;
    }
}

core::sheet::CellKind parse_kind(const FilterField& field) noexcept {
    return field.kind == core::sheet::CellKind::Auto ? core::sheet::CellKind::Text : field.kind;
}

// One rule -> one comparison criterion.  `error` is set on failure.
bool build_criterion(const FilterField& field, FilterOp op, const std::string& raw_value,
                     const core::sheet::SheetFormat& format, FilterCriterion& out,
                     std::string& error) {
    if (op == FilterOp::IsNull) {
        out = FilterCriterion::is_null(field.field);
        return true;
    }
    if (op == FilterOp::IsNotNull) {
        out = FilterCriterion::is_not_null(field.field);
        return true;
    }
    if (op == FilterOp::IsTrue || op == FilterOp::IsFalse) {
        out = FilterCriterion::comparison(field.field, op,
                                          Value::of_bool(op == FilterOp::IsTrue));
        return true;
    }
    if (trim(raw_value).empty()) {
        error = "enter a value for " + field.title;
        return false;
    }
    const core::sheet::EditOutcome parsed =
        core::sheet::parse_edit_text(raw_value, parse_kind(field), format);
    if (!parsed.ok) {
        error = field.title + ": " + parsed.error;
        return false;
    }
    out = FilterCriterion::comparison(field.field, op, parsed.value);
    return true;
}

}  // namespace

const std::vector<FilterOperatorChoice>& filter_operators() {
    static const std::vector<FilterOperatorChoice> operators = {
        {FilterOp::Equals, "equals"},
        {FilterOp::NotEquals, "does not equal"},
        {FilterOp::Contains, "contains"},
        {FilterOp::NotContains, "does not contain"},
        {FilterOp::StartsWith, "starts with"},
        {FilterOp::EndsWith, "ends with"},
        {FilterOp::LessThan, "is less than"},
        {FilterOp::LessOrEqual, "is at most"},
        {FilterOp::GreaterThan, "is greater than"},
        {FilterOp::GreaterOrEqual, "is at least"},
        {FilterOp::IsNull, "is blank"},
        {FilterOp::IsNotNull, "is not blank"},
    };
    return operators;
}

std::vector<FilterField> filter_fields(const core::sheet::SheetModel& sheet) {
    std::vector<FilterField> fields;
    fields.reserve(sheet.visible_column_count());
    for (const std::size_t column : sheet.visible_columns()) {
        if (column >= sheet.column_count()) {
            continue;
        }
        const core::sheet::SheetColumn& definition = sheet.column(column);
        if (definition.field == 0) {
            continue;
        }
        FilterField entry;
        entry.field = definition.field;
        entry.title = sheet.column_title(column);
        entry.kind = definition.kind;
        if (entry.kind == core::sheet::CellKind::Auto) {
            entry.kind = core::sheet::field_cell_kind(definition.field);
        }
        fields.push_back(std::move(entry));
    }
    return fields;
}

std::string describe_filter(const std::vector<FilterField>& fields,
                            const std::vector<FilterOperatorChoice>& operators,
                            const std::vector<FilterRule>& rules, FilterJoin join) {
    std::string text;
    for (const FilterRule& rule : rules) {
        if (rule.field_index >= fields.size() || rule.operator_index >= operators.size()) {
            continue;
        }
        const FilterField& field = fields[rule.field_index];
        const FilterOperatorChoice& choice = operators[rule.operator_index];
        if (!text.empty()) {
            text += join == FilterJoin::all ? " and " : " or ";
        }
        text += field.title;
        text += " ";
        text += choice.title;
        if (op_takes_value(choice.op)) {
            text += " ";
            text += quoted(trim(rule.value));
        }
    }
    return text;
}

FilterBuild build_filter(const std::vector<FilterField>& fields,
                         const std::vector<FilterOperatorChoice>& operators,
                         const std::vector<FilterRule>& rules,
                         const core::sheet::SheetFormat& format, FilterJoin join) {
    FilterBuild build;
    if (fields.empty()) {
        build.error = "the sheet has no filterable column";
        return build;
    }
    if (operators.empty()) {
        build.error = "no filter operators are configured";
        return build;
    }
    if (rules.empty()) {
        build.error = "add a criterion before applying the filter";
        return build;
    }

    std::vector<FilterCriterion> criteria;
    criteria.reserve(rules.size());
    for (const FilterRule& rule : rules) {
        if (rule.field_index >= fields.size()) {
            build.error = "filter field is out of range";
            return build;
        }
        if (rule.operator_index >= operators.size()) {
            build.error = "filter operator is out of range";
            return build;
        }
        FilterCriterion criterion;
        std::string error;
        if (!build_criterion(fields[rule.field_index], operators[rule.operator_index].op,
                             rule.value, format, criterion, error)) {
            build.error = std::move(error);
            return build;
        }
        criteria.push_back(std::move(criterion));
    }

    build.definition.id = "app.filter";
    build.definition.record_type = core::model::RecordType::Task;
    build.description = describe_filter(fields, operators, rules, join);
    build.definition.name = build.description;
    if (criteria.size() == 1) {
        build.definition.criteria = criteria.front();
    } else if (join == FilterJoin::all) {
        build.definition.criteria = FilterCriterion::all(std::move(criteria));
    } else {
        build.definition.criteria = FilterCriterion::any(std::move(criteria));
    }
    build.ok = true;
    return build;
}

std::vector<std::size_t> filter_rows(const core::view::FilterDefinition& filter,
                                     const core::view::RecordSet& records) {
    return core::view::matching_rows(filter, records);
}

core::view::RecordSet filtered_record_set(const core::view::RecordSet& records,
                                          const std::vector<std::size_t>& rows) {
    core::view::RecordSet subset(records.record_type());
    const std::vector<core::view::FieldId>& fields = records.fields();
    subset.reserve(rows.size(), fields.size());
    for (const core::view::FieldId field : fields) {
        subset.add_field(field);
    }
    for (const std::size_t row : rows) {
        if (row >= records.row_count()) {
            continue;
        }
        const std::size_t target = subset.add_row(records.uid(row));
        for (const core::view::FieldId field : fields) {
            if (const Value* value = records.get(row, field); value != nullptr) {
                subset.set_field(target, field, *value);
            }
        }
    }
    return subset;
}

std::vector<std::size_t> restrict_to_filter(const std::vector<std::size_t>& rows,
                                            const std::vector<std::size_t>& filter_rows_in) {
    std::vector<std::size_t> sorted = filter_rows_in;
    std::sort(sorted.begin(), sorted.end());
    std::vector<std::size_t> kept;
    kept.reserve(rows.size());
    for (const std::size_t row : rows) {
        if (std::binary_search(sorted.begin(), sorted.end(), row)) {
            kept.push_back(row);
        }
    }
    return kept;
}

}  // namespace app
