#include "field_ref.h"

#include <cstdio>

namespace core::view {
namespace {

const core::model::FieldInfo* find_info(FieldId field) {
    return core::model::find_field_by_id(field);
}

std::string hex_field_id(FieldId field) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", field);
    return std::string(buffer);
}

}  // namespace

std::string field_name(FieldId field, core::model::RecordType record_type) {
    const core::model::FieldInfo* info = find_info(field);
    if (info == nullptr || info->record_type != static_cast<std::uint8_t>(record_type)) {
        return {};
    }
    return std::string(info->name);
}

std::string field_title(FieldId field, core::model::RecordType record_type,
                        std::string_view explicit_title) {
    if (!explicit_title.empty()) {
        return std::string(explicit_title);
    }
    std::string name = field_name(field, record_type);
    if (!name.empty()) {
        return name;
    }
    return hex_field_id(field);
}

ValueKind field_value_kind(FieldId field) {
    const core::model::FieldInfo* info = find_info(field);
    if (info == nullptr) {
        return ValueKind::Null;
    }
    const std::string_view type(info->engine_type_name);
    if (type == "text") return ValueKind::Text;
    if (type == "bool") return ValueKind::Bool;
    if (type == "i16" || type == "i32" || type == "i64") return ValueKind::Integer;
    if (type == "date32") return ValueKind::Date;
    if (type == "comp_dur") return ValueKind::Duration;
    return ValueKind::Null;
}

bool field_id_by_name(core::model::RecordType record_type, std::string_view name,
                      FieldId& field) {
    if (name.empty()) {
        return false;
    }
    const std::string key(name);
    const core::model::FieldInfo* info =
        core::model::find_field_by_name(key.c_str(), record_type);
    if (info == nullptr) {
        return false;
    }
    field = info->field_id;
    return true;
}

bool field_ref_from_json(const JsonValue& json, core::model::RecordType record_type,
                         FieldId& field, std::string_view path, std::string& error) {
    const std::string prefix = path.empty() ? std::string() : std::string(path) + ": ";
    if (!json.is_object()) {
        error = prefix + "field reference must be an object";
        return false;
    }
    const JsonValue* by_name = json.find("field");
    const JsonValue* by_id = json.find("field_id");
    if (by_name != nullptr && by_id != nullptr) {
        error = prefix + "field reference must use \"field\" or \"field_id\", not both";
        return false;
    }
    if (by_name != nullptr) {
        if (!by_name->is_string() || by_name->as_string().empty()) {
            error = prefix + "\"field\" must be a non-empty string";
            return false;
        }
        if (!field_id_by_name(record_type, by_name->as_string(), field)) {
            error = prefix + "unknown " + core::model::record_type_name(record_type) +
                    " field name \"" + by_name->as_string() + "\"";
            return false;
        }
        return true;
    }
    if (by_id != nullptr) {
        if (!by_id->is_number()) {
            error = prefix + "\"field_id\" must be a number";
            return false;
        }
        const double raw = by_id->as_number();
        if (raw < 1.0 || raw > 4294967295.0 || raw != static_cast<double>(static_cast<std::uint32_t>(raw))) {
            error = prefix + "\"field_id\" is not a 32-bit field id";
            return false;
        }
        const FieldId candidate = static_cast<FieldId>(raw);
        const core::model::FieldInfo* info = find_info(candidate);
        if (info == nullptr) {
            error = prefix + "unknown field id " + hex_field_id(candidate);
            return false;
        }
        if (info->record_type != static_cast<std::uint8_t>(record_type)) {
            error = prefix + "field id " + hex_field_id(candidate) + " belongs to " +
                    core::model::record_type_name(
                        static_cast<core::model::RecordType>(info->record_type)) +
                    ", not " + core::model::record_type_name(record_type);
            return false;
        }
        field = candidate;
        return true;
    }
    error = prefix + "field reference needs \"field\" or \"field_id\"";
    return false;
}

JsonValue field_ref_to_json(FieldId field, core::model::RecordType record_type) {
    JsonValue json = JsonValue::object();
    const std::string name = field_name(field, record_type);
    if (!name.empty()) {
        json.set("field", JsonValue::string(name));
    } else {
        json.set("field_id", JsonValue::integer(static_cast<std::int64_t>(field)));
    }
    return json;
}

bool record_type_from_name(std::string_view name, core::model::RecordType& out) noexcept {
    using core::model::RecordType;
    if (name == "Task") { out = RecordType::Task; return true; }
    if (name == "Resource") { out = RecordType::Resource; return true; }
    if (name == "Assignment") { out = RecordType::Assignment; return true; }
    if (name == "Links") { out = RecordType::Links; return true; }
    if (name == "Calendar") { out = RecordType::Calendar; return true; }
    if (name == "Project") { out = RecordType::Project; return true; }
    if (name == "OutlineCode") { out = RecordType::OutlineCode; return true; }
    if (name == "Task34") { out = RecordType::Task34; return true; }
    return false;
}

}  // namespace core::view
