/*
 * table_registry.h -- the table definitions Microsoft Project itself ships.
 *
 * PROVENANCE, AND WHY THIS IS DIFFERENT FROM defaults.h
 * -----------------------------------------------------
 * defaults.h exposes one recovered *catalogue* and three tables that were
 * plainly marked "inferred".  This header exposes the real thing: PJINTL.DLL's
 * RT_RCDATA resources 330 (Task tables) and 311 (Resource tables), which carry
 * one record per table:
 *
 *     UTF-16 table name (e.g. "&Entry")
 *     ...104-byte fixed name area...
 *     6-byte block header: 01 01 <column_count> <flag> ff 00
 *     <column_count> * 8-byte column records:
 *         u32 PjField id
 *         u16 width in characters
 *         u8  alignment code (0 left, 1 right, 2 centre)
 *         u8  format code (0 text, 1 integer/bool, 2 number/date/duration)
 *     u32 zero terminator
 *     UTF-16 custom captions, in the order the resource emits them
 *
 * The .bin files are git-ignored (see the `analysis/res` rule in `.gitignore`), so the
 * decoded data is committed here; memory/view.md records the exact offsets and
 * the decoder used for the W42 session.  The *column sets, widths and alignment
 * codes are measured* from that resource.  The caption->field assignment is an
 * inference only where the resource does not carry the field id next to the
 * caption (see memory/view.md); the captions themselves are the resource's
 * own strings.
 *
 * Fields whose COM property name the field table never recovered
 * (Indicators, Predecessors, Successors, ResourceNames, UniqueID) get a title
 * from the PjField enum name in analysis/typelib.tsv.  Every other column
 * falls back to the descriptor table's name, exactly like table.h.
 *
 * This is additive to defaults.h: the old (inferred) functions and the
 * recovered catalogue stay untouched so the macOS shell keeps compiling.
 */

#ifndef CORE_VIEW_TABLE_REGISTRY_H
#define CORE_VIEW_TABLE_REGISTRY_H

#include <string_view>
#include <vector>

#include "table.h"

namespace core::view::registry {

/* The 15 Task tables (RT_RCDATA 330, Export excluded: it is a data-export
 * definition with 84 columns, not a sheet the user opens). */
const std::vector<TableDefinition>& task_tables();

/* The 9 Resource tables (RT_RCDATA 311, Export excluded). */
const std::vector<TableDefinition>& resource_tables();

/* Task tables first, then resource tables.  Process lifetime. */
const std::vector<TableDefinition>& all_tables();

/* Exact lookup by the stable id ("entry", "schedule", "resource-entry", ...).
 * Returns the first match by name when `id` is not an id, so callers that only
 * know the display name still work; use the two-argument form when the record
 * type matters ("Entry" exists for Task and Resource). */
const TableDefinition* find_table(std::string_view id) noexcept;
const TableDefinition* find_table(std::string_view name,
                                  core::model::RecordType record_type) noexcept;

}  // namespace core::view::registry

#endif  // CORE_VIEW_TABLE_REGISTRY_H
