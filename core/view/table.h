/*
 * table.h -- a table is an ordered list of fields, plus presentation width.
 *
 * This is configuration, not UI: no widgets, no fonts, no drawing.  The sheet,
 * the Gantt's left pane, a usage table and CSV export all consume the same
 * TableDefinition.  A Column carries only what affects the *data* surface:
 *
 *   field   which PjField to read
 *   title   optional override; empty means "the field's name"
 *   width   in characters (0 = compute) -- stored, never interpreted here
 *   align   Auto means "by value kind": numeric/duration right, date left, text
 *           left.  An explicit value is honoured verbatim.
 *
 * Widths and alignment are MS Project view state.  No default widths are
 * invented for the canonical tables: the one table built from recovered data
 * (defaults.cpp) uses width 0 / Auto, and the caller may override.
 */

#ifndef CORE_VIEW_TABLE_H
#define CORE_VIEW_TABLE_H

#include <string>
#include <string_view>
#include <vector>

#include "field_ref.h"
#include "json.h"
#include "value.h"

namespace core::view {

enum class ColumnAlign : std::uint8_t { Auto = 0, Left, Center, Right };

const char* column_align_name(ColumnAlign align) noexcept;
bool column_align_from_name(std::string_view name, ColumnAlign& out) noexcept;

struct Column {
    FieldId field = 0;
    std::string title;            // empty = derive from the descriptor table
    int width = 0;                // characters; 0 = auto
    ColumnAlign align = ColumnAlign::Auto;

    std::string display_title(core::model::RecordType record_type) const {
        return field_title(field, record_type, title);
    }
};

struct TableDefinition {
    std::string id;
    std::string name;
    std::string source;                                   // provenance note, free text
    core::model::RecordType record_type = core::model::RecordType::Task;
    std::vector<Column> columns;

    const Column* find(FieldId field) const noexcept;
    bool has(FieldId field) const noexcept { return find(field) != nullptr; }
};

/*
 * JSON shape (all keys are optional except id/name/columns):
 * {
 *   "id": "entry", "name": "Entry",
 *   "record_type": "Task",
 *   "source": "inferred: ...",
 *   "columns": [ {"field":"ID","width":6,"align":"right"}, ... ]
 * }
 * A column may write {"field_id": 188743700} instead of {"field": "..."}.
 * Unknown keys are ignored so old readers can open newer definitions.
 */
JsonValue table_to_json(const TableDefinition& table);
bool table_from_json(const JsonValue& json, TableDefinition& table, std::string_view path,
                     std::string& error);

/* Writes a table as JSON. */
std::string table_to_json_string(const TableDefinition& table, bool pretty = true);

}  // namespace core::view

#endif  // CORE_VIEW_TABLE_H
