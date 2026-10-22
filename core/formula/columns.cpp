#include "columns.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "fields.h"

namespace core::formula {
namespace {

std::string hex_field_id(core::view::FieldId field) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", field);
    return std::string(buffer);
}

}  // namespace

bool ComputedColumns::add(const ComputedColumn& column, std::string& error) {
    if (column.field_id == 0) {
        error = "computed column needs a field id";
        return false;
    }
    const core::model::FieldInfo* info = core::model::find_field_by_id(column.field_id);
    if (info == nullptr) {
        error = "computed column targets unknown field id " + hex_field_id(column.field_id);
        return false;
    }
    if (index_of(column.field_id) >= 0) {
        error = "field " + hex_field_id(column.field_id) +
                " already has a computed column in this set";
        return false;
    }
    const auto record_type = static_cast<core::model::RecordType>(info->record_type);
    ParseResult parsed = parse(column.formula, record_type);
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }
    Entry entry;
    entry.column = column;
    entry.ast = std::move(parsed.root);
    entries_.push_back(std::move(entry));
    return true;
}

bool ComputedColumns::add(core::view::FieldId field, std::string name, std::string_view formula,
                          std::string& error) {
    ComputedColumn column;
    column.field_id = field;
    column.name = std::move(name);
    column.formula = std::string(formula);
    return add(column, error);
}

const ComputedColumn& ComputedColumns::column(std::size_t index) const noexcept {
    static const ComputedColumn kEmpty{};
    if (index >= entries_.size()) {
        return kEmpty;
    }
    return entries_[index].column;
}

const Expr* ComputedColumns::expression(std::size_t index) const noexcept {
    if (index >= entries_.size()) {
        return nullptr;
    }
    return &entries_[index].ast;
}

int ComputedColumns::index_of(core::view::FieldId field) const noexcept {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].column.field_id == field) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

core::view::Value ComputedColumns::value_at(const core::view::RecordSet& records, std::size_t row,
                                            core::view::FieldId field, int depth) const {
    const int index = index_of(field);
    if (index < 0) {
        return base_value_of_field(records, row, field);
    }
    if (depth > max_depth_) {
        return core::view::Value::null();
    }
    const Entry& entry = entries_[static_cast<std::size_t>(index)];
    EvalContext context;
    context.records = &records;
    context.row = row;
    context.record_type = records.record_type();
    context.format = format_;
    context.depth = depth;
    context.max_depth = max_depth_;
    context.resolve = [this, &records, row, depth](core::view::FieldId referenced) {
        return value_at(records, row, referenced, depth + 1);
    };
    const core::formula::EvalResult result = core::formula::evaluate(entry.ast, context);
    return result.ok ? result.value : core::view::Value::null();
}

core::view::Value ComputedColumns::value_of_field(const core::view::RecordSet& records,
                                                  std::size_t row,
                                                  core::view::FieldId field) const {
    return value_at(records, row, field, 0);
}

std::size_t ComputedColumns::evaluate(core::view::RecordSet& records) const {
    std::size_t written = 0;
    for (const Entry& entry : entries_) {
        if (!records.has_field(entry.column.field_id)) {
            (void)records.add_field(entry.column.field_id);
        }
        for (std::size_t row = 0; row < records.row_count(); ++row) {
            records.set_field(row, entry.column.field_id,
                              value_at(records, row, entry.column.field_id, 0));
            ++written;
        }
    }
    return written;
}

bool ComputedColumns::add_to_sheet(core::sheet::SheetModel& sheet, core::view::FieldId field,
                                   std::string title, std::string_view formula,
                                   std::string& error) const {
    ComputedColumns local = *this;
    if (!local.add(field, title, formula, error)) {
        return false;
    }
    local.evaluate(sheet.records());

    std::vector<core::sheet::SheetColumn> columns;
    columns.reserve(sheet.column_count() + 1);
    bool present = false;
    for (std::size_t i = 0; i < sheet.column_count(); ++i) {
        const core::sheet::SheetColumn& existing = sheet.column(i);
        if (existing.field == field) {
            present = true;
        }
        columns.push_back(existing);
    }
    if (!present) {
        core::sheet::SheetColumn added;
        added.field = field;
        added.title = std::move(title);
        columns.push_back(std::move(added));
    }
    sheet.set_columns(std::move(columns));
    sheet.refresh();
    return true;
}

}  // namespace core::formula
