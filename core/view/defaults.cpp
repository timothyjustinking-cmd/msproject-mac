#include "defaults.h"

#include <iterator>
#include <utility>

namespace core::view::defaults {
namespace {

/* analysis/column_field_map.tsv, in file order.  The ordinal is the recovered
 * column index; 19 is absent in the source, so there are 41 rows, not 42.  Keep
 * this table identical to the TSV: view_model_tests re-reads the TSV and fails
 * if the two disagree. */
struct RecoveredColumn {
    int ordinal;
    FieldId field;
};

constexpr RecoveredColumn kRecovered[] = {
    {0, 0x0b40000e},  // Name
    {1, 0x0b400056},
    {2, 0x0b400017},  // ID
    {3, 0x0b40009f},
    {4, 0x0b4000c7},
    {5, 0x0b40000f},  // Notes
    {6, 0x0b4000c2},
    {7, 0x0b400055},  // OutlineLevel
    {8, 0x0b40005c},  // Summary
    {9, 0x0b4000e4},
    {10, 0x0b4000c6},
    {11, 0x0b4000e0},
    {12, 0x0b400087},
    {13, 0x0b4000d2},
    {14, 0x0b4000d3},
    {15, 0x0b4000d4},
    {16, 0x0b400062},  // LinkedFields
    {17, 0x0b400090},
    {18, 0x0b4000e3},
    {20, 0x0b4000f1},
    {21, 0x0b4000f7},
    {22, 0x0b4000d6},  // Application
    {23, 0x0b4000d5},  // Parent
    {24, 0x0b4000ed},
    {25, 0x0b4000ec},
    {26, 0x0b4000f5},
    {27, 0x0b4000e5},
    {28, 0x0b4000e6},
    {29, 0x0b4000d1},
    {30, 0x0b400083},
    {31, 0x0b40017f},
    {32, 0x0b400181},
    {33, 0x0b4001be},  // GroupBySummary
    {34, 0x0b4001bf},
    {35, 0x0b4001c0},
    {36, 0x0b4001ba},
    {37, 0x0b4001bb},
    {38, 0x0b4001bc},
    {39, 0x0b4001c6},
    {40, 0x0b400464},
    {41, 0x0b4004ff},  // Active
};

TableDefinition make_recovered() {
    TableDefinition table;
    table.id = "recovered-column-map";
    table.name = "Recovered column map";
    table.record_type = core::model::RecordType::Task;
    table.source =
        "evidence: analysis/column_field_map.tsv (41 pairs from WINPROJ.EXE VA "
        "0x1413b0440, scripts/phase2d_column_map.py); ordinal 19 absent in source";
    table.columns.reserve(std::size(kRecovered));
    for (const auto& entry : kRecovered) {
        Column column;
        column.field = entry.field;
        column.width = 0;
        column.align = ColumnAlign::Auto;
        table.columns.push_back(std::move(column));
    }
    return table;
}

/* Adds a column by descriptor name.  Returns false when the analysis has no
 * such named field, which keeps inferred tables honest as the table evolves. */
bool add_named(TableDefinition& table, const char* name) {
    FieldId field = 0;
    if (!field_id_by_name(table.record_type, name, field)) {
        return false;
    }
    Column column;
    column.field = field;
    table.columns.push_back(std::move(column));
    return true;
}

TableDefinition make_entry() {
    TableDefinition table;
    table.id = "entry";
    table.name = "Entry";
    table.record_type = core::model::RecordType::Task;
    table.source =
        "inferred: MS Project default Entry (Task Sheet) columns; NOT oracle-verified. "
        "Indicators is omitted because the descriptor table has no named Indicators field.";
    add_named(table, "ID");
    add_named(table, "Name");
    add_named(table, "Duration");
    add_named(table, "Start");
    add_named(table, "Finish");
    add_named(table, "Predecessors");
    add_named(table, "ResourceNames");
    return table;
}

TableDefinition make_summary() {
    TableDefinition table;
    table.id = "summary";
    table.name = "Summary";
    table.record_type = core::model::RecordType::Task;
    table.source =
        "inferred: MS Project default Summary table columns; NOT oracle-verified.";
    add_named(table, "ID");
    add_named(table, "Name");
    add_named(table, "Duration");
    add_named(table, "Start");
    add_named(table, "Finish");
    add_named(table, "PercentComplete");
    return table;
}

TableDefinition make_cost() {
    TableDefinition table;
    table.id = "cost";
    table.name = "Cost";
    table.record_type = core::model::RecordType::Task;
    table.source =
        "inferred: MS Project default Cost table columns; NOT oracle-verified.";
    add_named(table, "ID");
    add_named(table, "Name");
    add_named(table, "FixedCost");
    add_named(table, "FixedCostAccrual");
    add_named(table, "Cost");
    add_named(table, "BaselineCost");
    add_named(table, "ActualCost");
    add_named(table, "RemainingCost");
    return table;
}

const std::vector<TableDefinition>& tables() {
    static const std::vector<TableDefinition> kTables = {make_recovered(), make_entry(),
                                                         make_summary(), make_cost()};
    return kTables;
}

}  // namespace

const TableDefinition& recovered_column_map() { return tables()[0]; }

const TableDefinition& inferred_entry() { return tables()[1]; }

const TableDefinition& inferred_summary() { return tables()[2]; }

const TableDefinition& inferred_cost() { return tables()[3]; }

const std::vector<TableDefinition>& builtin_tables() { return tables(); }

const TableDefinition* find_builtin_table(std::string_view id) noexcept {
    for (const auto& table : tables()) {
        if (table.id == id) {
            return &table;
        }
    }
    return nullptr;
}

}  // namespace core::view::defaults
