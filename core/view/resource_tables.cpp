// core/view/resource_tables.cpp -- resource RecordSet builder and table lookup.
//
// The table data is W42's measured decode (table_registry.cpp); this file only
// filters, validates and pairs it with the neutral builder.  See
// resource_tables.h for provenance and the field-kind decision.

#include "resource_tables.h"

#include <cstddef>
#include <cstdio>

#include "field_table.h"
#include "table_registry.h"

namespace core::view {
namespace {

constexpr FieldId kBuilderFields[] = {
    kResourceIdField,
    kResourceNameField,
    kResourceTypeField,
    kResourceInitialsField,
    kResourceMaxUnitsField,
};

std::string hex_field(FieldId field) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08x", field);
    return std::string(buffer);
}

}  // namespace

const char* resource_type_name(ResourceType type) noexcept {
    switch (type) {
        case ResourceType::work:
            return "Work";
        case ResourceType::material:
            return "Material";
        case ResourceType::cost:
            return "Cost";
    }
    return "Work";
}

ValueKind resource_field_kind(FieldId field) noexcept {
    switch (field) {
        case kResourceIdField:
        case kResourceUniqueIdField:
        case kResourceTypeField:
            return ValueKind::Integer;
        case kResourceNameField:
        case kResourceInitialsField:
            return ValueKind::Text;
        case kResourceMaxUnitsField:
            return ValueKind::Number;
        default:
            return ValueKind::Null;
    }
}

RecordSet build_resource_record_set(const std::vector<ResourceRecord>& resources) {
    RecordSet set(core::model::RecordType::Resource);
    for (const FieldId field : kBuilderFields) {
        set.add_field(field);
    }
    for (const ResourceRecord& resource : resources) {
        const std::size_t row = set.add_row(resource.uid);
        set.set_field(row, kResourceIdField, Value::of_int(resource.id));
        set.set_field(row, kResourceNameField, Value::of_text(resource.name));
        set.set_field(row, kResourceTypeField,
                      Value::of_int(static_cast<std::int64_t>(resource.type)));
        set.set_field(row, kResourceInitialsField, Value::of_text(resource.initials));
        set.set_field(row, kResourceMaxUnitsField,
                      Value::of_number(resource.max_units_percent));
    }
    return set;
}

const std::vector<TableDefinition>& resource_table_definitions() noexcept {
    return registry::resource_tables();
}

const TableDefinition* find_resource_table(std::string_view id_or_name) noexcept {
    const std::vector<TableDefinition>& tables = registry::resource_tables();
    for (const TableDefinition& table : tables) {
        if (table.id == id_or_name) {
            return &table;
        }
    }
    for (const TableDefinition& table : tables) {
        if (table.name == id_or_name) {
            return &table;
        }
    }
    return nullptr;
}

bool resource_tables_resolve(std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    const std::vector<TableDefinition>& tables = registry::resource_tables();
    for (const TableDefinition& table : tables) {
        if (table.record_type != core::model::RecordType::Resource) {
            if (error != nullptr) {
                *error = "resource table " + table.id + " has record type " +
                         core::model::record_type_name(table.record_type);
            }
            return false;
        }
        if (table.columns.empty()) {
            if (error != nullptr) {
                *error = "resource table " + table.id + " has no columns";
            }
            return false;
        }
        for (const Column& column : table.columns) {
            const core::model::FieldInfo* info =
                core::model::find_field_by_id(column.field);
            if (info == nullptr) {
                if (error != nullptr) {
                    *error = "resource table " + table.id + " column " +
                             hex_field(column.field) + " is not in the descriptor table";
                }
                return false;
            }
            if (info->record_type !=
                static_cast<std::uint8_t>(core::model::RecordType::Resource)) {
                if (error != nullptr) {
                    *error = "resource table " + table.id + " column " +
                             hex_field(column.field) + " is a " +
                             core::model::record_type_name(
                                 static_cast<core::model::RecordType>(info->record_type)) +
                             " field";
                }
                return false;
            }
            if (info->engine_type_name == nullptr || info->engine_type_name[0] == '\0') {
                if (error != nullptr) {
                    *error = "resource table " + table.id + " column " +
                             hex_field(column.field) + " has no storage type";
                }
                return false;
            }
        }
    }
    return true;
}

}  // namespace core::view
