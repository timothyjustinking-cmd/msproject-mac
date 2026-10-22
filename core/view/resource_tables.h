// core/view/resource_tables.h -- the resource RecordSet builder and the
// measured resource table set (W43).
//
// PROVENANCE
// ----------
// The resource table definitions are already decoded: W42 read PJINTL.DLL
// RT_RCDATA 311 and core/view/table_registry.h exposes the nine resource
// sheets (Entry, Usage, Cost, Work, Summary, Hyperlink, Earned Value and the
// two Entry filters).  This file does not re-decode them.  It adds the two
// things a resource view still needs:
//
//   * a neutral ResourceRecord and build_resource_record_set(), so a caller can
//     turn the reader's resources (core/mpp) into a RecordSet without core/view
//     depending on core/mpp;
//   * lookup/validation helpers over the resource tables, so a renderer resolves
//     the same captions, widths and field ids the Resource sheet uses, and a
//     test can pin that every column resolves through the descriptor table.
//
// FIELD KINDS are the ones core/io's MSPDI resource importer already uses
// (core/io/mspdi.cpp, kResourceFields): ID and Type are Integers, Name and
// Initials are Text, MaxUnits is a Number in PERCENT (100.0 == 100 %).  That is
// deliberately not field_value_kind(): the descriptor's storage type for
// MaxUnits is i64, but the view/interchange value is a fractional quantity, so
// this module states the kind explicitly and the test pins it.
//
// TYPE VALUES are Project's PjResourceTypes order -- 0 Work, 1 Material,
// 2 Cost -- which is also core/mpp's ResourceType (analysis/typelib.tsv).  It
// is NOT the MSPDI export spelling: Project's MSPDI export writes Type 1 for a
// Work resource (memory/reader.md).  The builder never guesses a type: the
// caller passes what the reader decoded (today always Work with
// type_stored == false).
//
// This is additive: no existing core/view or core/sheet API changes.

#ifndef CORE_VIEW_RESOURCE_TABLES_H
#define CORE_VIEW_RESOURCE_TABLES_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "record_set.h"
#include "table.h"

namespace core::view {

// Resource PjField ids used by the builder.  Named here so a caller does not
// have to spell magic hex.  (Resource UniqueID is not a table column; it is
// carried by RecordSet::uid / add_row.)
inline constexpr FieldId kResourceIdField = 0x0c400000u;
inline constexpr FieldId kResourceNameField = 0x0c400001u;
inline constexpr FieldId kResourceInitialsField = 0x0c400002u;
inline constexpr FieldId kResourceMaxUnitsField = 0x0c400004u;
inline constexpr FieldId kResourceUniqueIdField = 0x0c40001bu;
inline constexpr FieldId kResourceTypeField = 0x0c40012cu;

// Project's PjResourceTypes order (see the header comment).
enum class ResourceType : std::uint8_t { work = 0, material = 1, cost = 2 };

const char* resource_type_name(ResourceType type) noexcept;

// One resource as the builder needs it.  No core/mpp type appears here.
struct ResourceRecord {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;
    std::string name;
    std::string initials;
    ResourceType type = ResourceType::work;
    // MaxUnits in percent: 100.0 == 100 % (the MSPDI importer's convention).
    double max_units_percent = 100.0;
};

// The Value kind build_resource_record_set() writes for a resource field, or
// ValueKind::Null when the field is not part of the builder's schema.
ValueKind resource_field_kind(FieldId field) noexcept;

// Build a Resource RecordSet with the Entry table's builder fields, in Entry
// order: ID, Name, Type, Initials, MaxUnits.  Row order is input order and
// RecordSet::uid(row) is ResourceRecord::uid.  The five columns are declared
// even when the input is empty, so a sheet's schema does not depend on data.
RecordSet build_resource_record_set(const std::vector<ResourceRecord>& resources);

// The nine resource tables decoded by W42 (RT_RCDATA 311, Export excluded).
// Same objects as core::view::registry::resource_tables(), process lifetime.
const std::vector<TableDefinition>& resource_table_definitions() noexcept;

// Lookup among the RESOURCE tables only: exact id first ("resource-entry"),
// then exact display name ("Entry").  nullptr when absent.  Unlike
// registry::find_table(name, record_type) this accepts an id too, and it can
// never return a Task table.
const TableDefinition* find_resource_table(std::string_view id_or_name) noexcept;

// True when every column of every resource table names a field that
// core::model::find_field_by_id() knows as a Resource field.  On failure the
// first problem is written to `error` when it is non-null.
bool resource_tables_resolve(std::string* error = nullptr);

}  // namespace core::view

#endif  // CORE_VIEW_RESOURCE_TABLES_H
