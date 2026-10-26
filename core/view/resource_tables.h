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
// WAVE 31 (W31-3.5): the builder now carries every value the Resource Sheet's
// stored Entry table shows -- Group, Code, Material Label, Base Calendar, the
// rates, Cost/Use and Accrue At -- plus Project's two computed resource fields,
// Overallocated and PeakUnits (0x0c400019 / 0x0c40001a).  The values are the
// READER's decoded values (core/mpp/reader.h Resource: the MPP14 rate words at
// +16/+24/+72/+80 and the Base Calendar resolved through TBkndCal); a caller
// that has no reader leaves the defaults.  `cost_fields_stored` keeps the
// MPP12 case honest: the rate words are not decoded there, so the builder
// stores Null rather than an invented 0.  PeakUnits uses the view's percent
// convention (300.0 == 300 %), the same as MaxUnits, and is the caller's
// computed maximum concurrent assignment load.
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
inline constexpr FieldId kResourceGroupField = 0x0c400003u;
inline constexpr FieldId kResourceMaxUnitsField = 0x0c400004u;
inline constexpr FieldId kResourceBaseCalendarField = 0x0c400005u;
inline constexpr FieldId kResourceStandardRateField = 0x0c400006u;
inline constexpr FieldId kResourceOvertimeRateField = 0x0c400007u;
inline constexpr FieldId kResourceCodeField = 0x0c40000au;
inline constexpr FieldId kResourceCostPerUseField = 0x0c400012u;
inline constexpr FieldId kResourceAccrueAtField = 0x0c400013u;
inline constexpr FieldId kResourceOverallocatedField = 0x0c400019u;
inline constexpr FieldId kResourcePeakUnitsField = 0x0c40001au;
inline constexpr FieldId kResourceUniqueIdField = 0x0c40001bu;
inline constexpr FieldId kResourceIndicatorsField = 0x0c400056u;
inline constexpr FieldId kResourceMaterialLabelField = 0x0c40012bu;
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

    // W31-3.5: the rest of the stored Entry table, in the reader's units.
    std::string group;
    std::string code;
    std::string material_label;
    std::string base_calendar;            // resolved calendar name, may be empty
    double standard_rate_per_hour = 0.0;  // currency units per hour
    double overtime_rate_per_hour = 0.0;  // currency units per hour
    double cost_per_use = 0.0;            // currency units once per use
    std::uint32_t accrue_at = 1;          // PjAccrueAt: 1 Start, 2 End, 3 Prorated
    bool cost_fields_stored = false;      // false -> rate/cost values are Null

    // Project's computed load fields, in the view's percent convention.
    double peak_units_percent = 0.0;      // PeakUnits (0x0c40001a)
    bool overallocated = false;           // Overallocated (0x0c400019)
};

// The Value kind build_resource_record_set() writes for a resource field, or
// ValueKind::Null when the field is not part of the builder's schema.
ValueKind resource_field_kind(FieldId field) noexcept;

// Build a Resource RecordSet with the stored Entry table's fields, in stored
// order minus Indicators (whose value is a status glyph, not a cell): ID, Name,
// Type, Material Label, Initials, Group, MaxUnits, Standard Rate, Overtime
// Rate, Cost/Use, Accrue At, Base Calendar, Code, then the two computed fields
// PeakUnits and Overallocated.  Row order is input order and
// RecordSet::uid(row) is ResourceRecord::uid.  The columns are declared even
// when the input is empty, so a sheet's schema does not depend on data; when
// `cost_fields_stored` is false the three cost fields hold Value::null() so a
// renderer cannot print an invented "$0.00".
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
