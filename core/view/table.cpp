#include "table.h"

#include <algorithm>

namespace core::view {

const char* column_align_name(ColumnAlign align) noexcept {
    switch (align) {
        case ColumnAlign::Auto: return "auto";
        case ColumnAlign::Left: return "left";
        case ColumnAlign::Center: return "center";
        case ColumnAlign::Right: return "right";
    }
    return "auto";
}

bool column_align_from_name(std::string_view name, ColumnAlign& out) noexcept {
    if (name == "auto") { out = ColumnAlign::Auto; return true; }
    if (name == "left") { out = ColumnAlign::Left; return true; }
    if (name == "center") { out = ColumnAlign::Center; return true; }
    if (name == "right") { out = ColumnAlign::Right; return true; }
    return false;
}

const Column* TableDefinition::find(FieldId field) const noexcept {
    for (const auto& column : columns) {
        if (column.field == field) {
            return &column;
        }
    }
    return nullptr;
}

JsonValue table_to_json(const TableDefinition& table) {
    JsonValue json = JsonValue::object();
    json.set("id", JsonValue::string(table.id));
    json.set("name", JsonValue::string(table.name));
    json.set("record_type",
             JsonValue::string(core::model::record_type_name(table.record_type)));
    if (!table.source.empty()) {
        json.set("source", JsonValue::string(table.source));
    }
    JsonValue columns = JsonValue::array();
    for (const auto& column : table.columns) {
        JsonValue item = field_ref_to_json(column.field, table.record_type);
        if (!column.title.empty()) {
            item.set("title", JsonValue::string(column.title));
        }
        if (column.width != 0) {
            item.set("width", JsonValue::integer(column.width));
        }
        if (column.align != ColumnAlign::Auto) {
            item.set("align", JsonValue::string(column_align_name(column.align)));
        }
        columns.push(std::move(item));
    }
    json.set("columns", std::move(columns));
    return json;
}

bool table_from_json(const JsonValue& json, TableDefinition& table, std::string_view path,
                     std::string& error) {
    const std::string prefix = path.empty() ? std::string() : std::string(path) + ": ";
    if (!json.is_object()) {
        error = prefix + "table must be an object";
        return false;
    }
    const JsonValue* id = json.find("id");
    const JsonValue* name = json.find("name");
    const JsonValue* columns = json.find("columns");
    if (id == nullptr || !id->is_string() || id->as_string().empty()) {
        error = prefix + "table needs a non-empty \"id\"";
        return false;
    }
    if (name == nullptr || !name->is_string()) {
        error = prefix + "table needs a \"name\" string";
        return false;
    }
    if (columns == nullptr || !columns->is_array()) {
        error = prefix + "table needs a \"columns\" array";
        return false;
    }

    TableDefinition parsed;
    parsed.id = id->as_string();
    parsed.name = name->as_string();
    if (const JsonValue* source = json.find("source")) {
        if (!source->is_string()) {
            error = prefix + "\"source\" must be a string";
            return false;
        }
        parsed.source = source->as_string();
    }
    if (const JsonValue* record_type = json.find("record_type")) {
        if (!record_type->is_string() ||
            !record_type_from_name(record_type->as_string(), parsed.record_type)) {
            error = prefix + "unknown \"record_type\"";
            return false;
        }
    }
    for (std::size_t i = 0; i < columns->items().size(); ++i) {
        const JsonValue& item = columns->items()[i];
        const std::string at = prefix + "columns[" + std::to_string(i) + "]";
        if (!item.is_object()) {
            error = at + ": column must be an object";
            return false;
        }
        Column column;
        if (!field_ref_from_json(item, parsed.record_type, column.field, at, error)) {
            return false;
        }
        if (const JsonValue* title = item.find("title")) {
            if (!title->is_string()) {
                error = at + ": \"title\" must be a string";
                return false;
            }
            column.title = title->as_string();
        }
        if (const JsonValue* width = item.find("width")) {
            if (!width->is_number()) {
                error = at + ": \"width\" must be a number";
                return false;
            }
            const double raw = width->as_number();
            if (raw < 0.0 || raw > 1000.0 || raw != static_cast<double>(static_cast<int>(raw))) {
                error = at + ": \"width\" must be an integer 0..1000";
                return false;
            }
            column.width = static_cast<int>(raw);
        }
        if (const JsonValue* align = item.find("align")) {
            if (!align->is_string() ||
                !column_align_from_name(align->as_string(), column.align)) {
                error = at + ": unknown \"align\"";
                return false;
            }
        }
        if (parsed.has(column.field)) {
            error = at + ": field appears twice in the table";
            return false;
        }
        parsed.columns.push_back(std::move(column));
    }
    table = std::move(parsed);
    return true;
}

std::string table_to_json_string(const TableDefinition& table, bool pretty) {
    return write_json(table_to_json(table), pretty);
}

}  // namespace core::view
