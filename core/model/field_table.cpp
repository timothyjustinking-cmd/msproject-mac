#include "field_table.h"

#include <cstring>

namespace core::model {
namespace {

const FieldInfo* find_by_name_in_impl(const char* name, std::uint8_t record_type,
                                      bool scoped) noexcept {
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (std::size_t i = 0; i < generated::kFieldCount; ++i) {
        const FieldInfo& field = generated::kFields[i];
        if (scoped && field.record_type != record_type) {
            continue;
        }
        if (std::strcmp(field.name, name) == 0) {
            return &field;
        }
    }
    return nullptr;
}

}  // namespace

std::size_t field_count() noexcept { return generated::kFieldCount; }

const FieldInfo* field_at(std::size_t index) noexcept {
    return index < generated::kFieldCount ? &generated::kFields[index] : nullptr;
}

const FieldInfo* find_field_by_id(std::uint32_t field_id) noexcept {
    for (std::size_t i = 0; i < generated::kFieldCount; ++i) {
        if (generated::kFields[i].field_id == field_id) {
            return &generated::kFields[i];
        }
    }
    return nullptr;
}

const FieldInfo* find_field_by_name(const char* name) noexcept {
    return find_by_name_in_impl(name, CORE_RECORD_UNKNOWN, false);
}

const FieldInfo* find_field_by_name(const char* name, RecordType record_type) noexcept {
    return find_by_name_in_impl(name, static_cast<std::uint8_t>(record_type), true);
}

const char* record_type_name(RecordType record_type) noexcept {
    switch (record_type) {
        case RecordType::Task:
            return "Task";
        case RecordType::Resource:
            return "Resource";
        case RecordType::Assignment:
            return "Assignment";
        case RecordType::Links:
            return "Links";
        case RecordType::Calendar:
            return "Calendar";
        case RecordType::Project:
            return "Project";
        case RecordType::OutlineCode:
            return "OutlineCode";
        case RecordType::Task34:
            return "Task34";
        case RecordType::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

}  // namespace core::model
