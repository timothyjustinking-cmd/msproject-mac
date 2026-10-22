/*
 * field_ref.h -- resolving the field references in view definitions.
 *
 * A view definition is authored against the PjField *name* that MS Project's
 * object model exposes ("Start", "PercentComplete"), because that is the only
 * stable, human-readable key we have.  The runtime works with the numeric field
 * id, because that is what the descriptor table and RecordSet use.  Resolution
 * goes through core/model/field_table.cpp, so a name that the analysis has not
 * decoded is a hard error rather than a silently blank column.
 *
 * Fields whose COM property name the analysis never recovered (1,444 of the
 * 3,207; 31 of the 41 recovered UI column mappings) can only be referenced by
 * numeric id.  Titles for those fall back to "0x0b4000c7".
 */

#ifndef CORE_VIEW_FIELD_REF_H
#define CORE_VIEW_FIELD_REF_H

#include <cstdint>
#include <string>
#include <string_view>

#include "field_table.h"
#include "json.h"
#include "value.h"

namespace core::view {

/* Empty when the descriptor table has no name for this field. */
std::string field_name(FieldId field, core::model::RecordType record_type);

/*
 * The label a column header or a group heading uses: the explicit title when
 * one was authored, otherwise the descriptor name, otherwise "0x%08x".
 */
std::string field_title(FieldId field, core::model::RecordType record_type,
                        std::string_view explicit_title = {});

/* Default storage kind from the descriptor table; Null when unknown/virtual. */
ValueKind field_value_kind(FieldId field);

/* Case-sensitive exact lookup, scoped to the record type.  False when unnamed. */
bool field_id_by_name(core::model::RecordType record_type, std::string_view name,
                      FieldId& field);

/*
 * JSON field reference.  Exactly one of:
 *   "field": "Start"                 (resolved in `record_type`)
 *   "field_id": 188743700           (numeric, for unnamed fields)
 * Missing/null/empty is an error.  `path` is prefixed to error messages.
 */
bool field_ref_from_json(const JsonValue& json, core::model::RecordType record_type,
                         FieldId& field, std::string_view path, std::string& error);

/* Writes {"field": "Start"} when named, otherwise {"field_id": <number>}. */
JsonValue field_ref_to_json(FieldId field, core::model::RecordType record_type);

/* Parses "Task", "Resource", ...; unknown strings fail.  The one-way name
 * function is core::model::record_type_name() -- re-exporting it here would make
 * every unqualified call ambiguous through argument-dependent lookup. */
bool record_type_from_name(std::string_view name, core::model::RecordType& out) noexcept;

}  // namespace core::view

#endif  // CORE_VIEW_FIELD_REF_H
