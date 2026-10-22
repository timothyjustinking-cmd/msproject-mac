/*
 * defaults.h -- view definitions we can justify.
 *
 * PROVENANCE MATTERS HERE.  The repository contains exactly one piece of
 * evidence about column sets: analysis/column_field_map.tsv, 41 (column
 * ordinal, PjField id) pairs recovered from WINPROJ.EXE VA 0x1413b0440 by
 * scripts/phase2d_column_map.py.  It is Task-only and mostly unnamed, and its
 * order (Name at 0, ID at 2) does not match MS Project's documented Entry
 * sheet, so it is a compiled column catalogue -- not a recovered default table.
 * recovered_column_map() exposes it verbatim, with no invented titles.
 *
 * The canonical tables (Entry/Summary/Cost) are what MS Project's own
 * documentation presents as defaults.  They are marked source="inferred" and
 * are NOT oracle-verified; the oracle recipe that would settle them is in
 * spec/view-model.md.  Nothing else is included.
 */

#ifndef CORE_VIEW_DEFAULTS_H
#define CORE_VIEW_DEFAULTS_H

#include <string_view>
#include <vector>

#include "table.h"

namespace core::view::defaults {

/* The evidenced catalogue: 41 Task columns in recovered ordinal order. */
const TableDefinition& recovered_column_map();

/* Inferred defaults, each with a non-empty source explaining that fact. */
const TableDefinition& inferred_entry();
const TableDefinition& inferred_summary();
const TableDefinition& inferred_cost();

/* recovered_column_map() first, then the inferred tables.  Process lifetime. */
const std::vector<TableDefinition>& builtin_tables();

const TableDefinition* find_builtin_table(std::string_view id) noexcept;

}  // namespace core::view::defaults

#endif  // CORE_VIEW_DEFAULTS_H
