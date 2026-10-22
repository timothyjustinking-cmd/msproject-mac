#include "table_registry.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace core::view::registry {
namespace {

struct RawColumn {
    FieldId field;
    int width;
    ColumnAlign align;
    const char* title;  // empty = derive from the descriptor table
};

/*
 * Decoded from PJINTL.DLL RT_RCDATA 330 (Task) and 311 (Resource).  The .bin
 * files are git-ignored; memory/view.md carries the decode recipe and the
 * conversion from the resource's alignment codes.  Do not hand-edit a width
 * without re-reading the resource.
 */

// ---------------------------------------------------------------------------
// Task tables -- RT_RCDATA 330
// ---------------------------------------------------------------------------

constexpr RawColumn kTaskEntry[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},              // ID
    {0x0b4000cdu, 6, ColumnAlign::Left, "Indicators"},     // Indicators
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},     // Name
    {0x0b40001du, 9, ColumnAlign::Right, ""},              // Duration
    {0x0b400023u, 12, ColumnAlign::Right, ""},             // Start
    {0x0b400024u, 12, ColumnAlign::Right, ""},             // Finish
    {0x0b40002fu, 14, ColumnAlign::Left, "Predecessors"},  // Predecessors
    {0x0b400031u, 17, ColumnAlign::Left, "Resource Names"},// ResourceNames
};

constexpr RawColumn kTaskVariance[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 20, ColumnAlign::Left, "Task Name"},
    {0x0b400023u, 12, ColumnAlign::Right, ""},
    {0x0b400024u, 12, ColumnAlign::Right, ""},
    {0x0b40002bu, 15, ColumnAlign::Right, ""},              // BaselineStart
    {0x0b40002cu, 15, ColumnAlign::Right, ""},              // BaselineFinish
    {0x0b40002du, 11, ColumnAlign::Right, "Start Var."},    // StartVariance
    {0x0b40002eu, 11, ColumnAlign::Right, "Finish Var."},   // FinishVariance
};

constexpr RawColumn kTaskEarnedValue[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b40000cu, 11, ColumnAlign::Right, ""},              // BCWS
    {0x0b40000bu, 11, ColumnAlign::Right, ""},              // BCWP
    {0x0b400078u, 11, ColumnAlign::Right, "ACWP"},
    {0x0b40000du, 11, ColumnAlign::Right, ""},              // SV
    {0x0b400053u, 11, ColumnAlign::Right, ""},              // CV
    {0x0b40021du, 11, ColumnAlign::Right, "EAC"},
    {0x0b400006u, 11, ColumnAlign::Right, "BAC"},           // BaselineCost
    {0x0b4001b9u, 11, ColumnAlign::Right, ""},              // VAC
};

constexpr RawColumn kTaskBaseline[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b40001bu, 13, ColumnAlign::Right, "Baseline Dur."},
    {0x0b40002bu, 15, ColumnAlign::Right, ""},
    {0x0b40002cu, 15, ColumnAlign::Right, ""},
    {0x0b400001u, 14, ColumnAlign::Right, ""},
    {0x0b400006u, 14, ColumnAlign::Right, ""},
};

constexpr RawColumn kTaskTracking[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 20, ColumnAlign::Left, "Task Name"},
    {0x0b400029u, 12, ColumnAlign::Right, "Act. Start"},
    {0x0b40002au, 12, ColumnAlign::Right, "Act. Finish"},
    {0x0b400020u, 9, ColumnAlign::Right, "% Comp."},
    {0x0b40045fu, 9, ColumnAlign::Center, "Phys. % Comp."},
    {0x0b40001cu, 9, ColumnAlign::Right, "Act. Dur."},
    {0x0b40001fu, 10, ColumnAlign::Right, "Rem. Dur."},
    {0x0b400007u, 11, ColumnAlign::Right, "Act. Cost"},
    {0x0b400002u, 10, ColumnAlign::Right, "Act. Work"},
};

constexpr RawColumn kTaskSchedule[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 20, ColumnAlign::Left, "Task Name"},
    {0x0b400023u, 12, ColumnAlign::Right, ""},
    {0x0b400024u, 12, ColumnAlign::Right, ""},
    {0x0b400027u, 12, ColumnAlign::Right, ""},  // LateStart
    {0x0b400028u, 12, ColumnAlign::Right, ""},  // LateFinish
    {0x0b400015u, 11, ColumnAlign::Right, ""},  // FreeSlack
    {0x0b400016u, 11, ColumnAlign::Right, ""},  // TotalSlack
};

constexpr RawColumn kTaskWork[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b400000u, 11, ColumnAlign::Right, ""},              // Work
    {0x0b400001u, 11, ColumnAlign::Right, "Baseline"},      // BaselineWork
    {0x0b400003u, 11, ColumnAlign::Right, "Variance"},      // WorkVariance
    {0x0b400002u, 11, ColumnAlign::Right, "Actual"},        // ActualWork
    {0x0b400004u, 11, ColumnAlign::Right, "Remaining"},     // RemainingWork
    {0x0b400021u, 12, ColumnAlign::Right, "% W. Comp."},    // PercentWorkComplete
};

constexpr RawColumn kTaskCost[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b400008u, 11, ColumnAlign::Right, ""},              // FixedCost
    {0x0b4000c8u, 17, ColumnAlign::Right, ""},              // FixedCostAccrual
    {0x0b400005u, 11, ColumnAlign::Right, "Total Cost"},    // Cost
    {0x0b400006u, 11, ColumnAlign::Right, "Baseline"},      // BaselineCost
    {0x0b400009u, 11, ColumnAlign::Right, "Variance"},      // CostVariance
    {0x0b400007u, 11, ColumnAlign::Right, "Actual"},        // ActualCost
    {0x0b40000au, 11, ColumnAlign::Right, "Remaining"},     // RemainingCost
};

constexpr RawColumn kTaskSummary[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b40001du, 12, ColumnAlign::Right, ""},
    {0x0b400023u, 12, ColumnAlign::Right, ""},
    {0x0b400024u, 12, ColumnAlign::Right, ""},
    {0x0b400020u, 9, ColumnAlign::Right, "% Comp."},
    {0x0b400005u, 11, ColumnAlign::Right, ""},
    {0x0b400000u, 10, ColumnAlign::Right, ""},
};

constexpr RawColumn kTaskConstraintDates[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b40001du, 9, ColumnAlign::Right, ""},
    {0x0b400011u, 19, ColumnAlign::Right, ""},  // ConstraintType
    {0x0b400012u, 15, ColumnAlign::Right, ""},  // ConstraintDate
};

constexpr RawColumn kTaskDelay[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b4000cdu, 6, ColumnAlign::Left, "Indicators"},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b400014u, 13, ColumnAlign::Right, ""},  // LevelingDelay
    {0x0b40001du, 9, ColumnAlign::Right, ""},
    {0x0b400023u, 12, ColumnAlign::Right, ""},
    {0x0b400024u, 12, ColumnAlign::Right, ""},
    {0x0b400030u, 14, ColumnAlign::Left, "Successors"},
    {0x0b400031u, 17, ColumnAlign::Left, "Resources"},
};

constexpr RawColumn kTaskUsage[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b4000cdu, 6, ColumnAlign::Left, "Indicators"},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b400000u, 10, ColumnAlign::Right, ""},
    {0x0b40001du, 10, ColumnAlign::Right, ""},
    {0x0b400023u, 12, ColumnAlign::Right, ""},
    {0x0b400024u, 12, ColumnAlign::Right, ""},
};

constexpr RawColumn kTaskHyperlink[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b4000cdu, 6, ColumnAlign::Left, "Indicators"},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b4000d9u, 17, ColumnAlign::Left, "Hyperlink"},
    {0x0b4000dau, 24, ColumnAlign::Left, "Address"},
    {0x0b4000dbu, 17, ColumnAlign::Left, "SubAddress"},
};

constexpr RawColumn kTaskEarnedValueSchedule[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b40000cu, 11, ColumnAlign::Right, ""},
    {0x0b40000bu, 11, ColumnAlign::Right, ""},
    {0x0b40000du, 11, ColumnAlign::Right, "SV"},
    {0x0b40021cu, 9, ColumnAlign::Right, "SV%"},
    {0x0b40021au, 9, ColumnAlign::Right, ""},  // SPI
};

constexpr RawColumn kTaskEarnedValueCost[] = {
    {0x0b400017u, 6, ColumnAlign::Right, ""},
    {0x0b40000eu, 24, ColumnAlign::Left, "Task Name"},
    {0x0b40000cu, 11, ColumnAlign::Right, ""},
    {0x0b40000bu, 11, ColumnAlign::Right, ""},
    {0x0b400053u, 11, ColumnAlign::Right, "CV"},
    {0x0b40021bu, 9, ColumnAlign::Right, "CV%"},
    {0x0b400219u, 9, ColumnAlign::Right, ""},  // CPI
    {0x0b400006u, 11, ColumnAlign::Right, "BAC"},
    {0x0b40021du, 11, ColumnAlign::Right, "EAC"},
    {0x0b4001b9u, 11, ColumnAlign::Right, "VAC"},
    {0x0b40021eu, 9, ColumnAlign::Right, ""},  // TCPI
};

// ---------------------------------------------------------------------------
// Resource tables -- RT_RCDATA 311
// ---------------------------------------------------------------------------

constexpr RawColumn kResourceEntry[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400056u, 6, ColumnAlign::Left, "Indicators"},
    {0x0c400001u, 20, ColumnAlign::Left, "Resource Name"},
    {0x0c4000fcu, 20, ColumnAlign::Left, ""},  // Phonetics
    {0x0c40012cu, 11, ColumnAlign::Left, ""},  // Type
    {0x0c40012bu, 13, ColumnAlign::Left, ""},  // MaterialLabel
    {0x0c400002u, 11, ColumnAlign::Left, ""},  // Initials
    {0x0c400003u, 10, ColumnAlign::Left, ""},  // Group
    {0x0c400004u, 10, ColumnAlign::Right, "Max. Units"},
    {0x0c400006u, 11, ColumnAlign::Right, "Std. Rate"},
    {0x0c400007u, 11, ColumnAlign::Right, "Ovt. Rate"},
    {0x0c400012u, 11, ColumnAlign::Right, "Cost/Use"},
    {0x0c400013u, 10, ColumnAlign::Left, ""},  // AccrueAt
    {0x0c400005u, 14, ColumnAlign::Left, ""},  // BaseCalendar
    {0x0c40000au, 13, ColumnAlign::Left, ""},  // Code
};

constexpr RawColumn kResourceUsage[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400056u, 6, ColumnAlign::Left, "Indicators"},
    {0x0c400001u, 20, ColumnAlign::Left, "Resource Name"},
    {0x0c40000du, 10, ColumnAlign::Right, ""},  // Work
};

constexpr RawColumn kResourceCost[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400001u, 20, ColumnAlign::Left, "Resource Name"},
    {0x0c40000cu, 14, ColumnAlign::Right, ""},              // Cost
    {0x0c400011u, 14, ColumnAlign::Right, ""},              // BaselineCost
    {0x0c400018u, 14, ColumnAlign::Right, "Variance"},      // CostVariance
    {0x0c40000bu, 14, ColumnAlign::Right, ""},              // ActualCost
    {0x0c400015u, 14, ColumnAlign::Right, "Remaining"},     // RemainingCost
};

constexpr RawColumn kResourceWork[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400001u, 20, ColumnAlign::Left, "Resource Name"},
    {0x0c40001du, 9, ColumnAlign::Right, "% Comp."},
    {0x0c40000du, 11, ColumnAlign::Right, ""},              // Work
    {0x0c400010u, 11, ColumnAlign::Right, "Overtime"},
    {0x0c40000fu, 11, ColumnAlign::Right, "Baseline"},
    {0x0c400017u, 11, ColumnAlign::Right, "Variance"},
    {0x0c40000eu, 11, ColumnAlign::Right, "Actual"},
    {0x0c400016u, 11, ColumnAlign::Right, "Remaining"},
};

constexpr RawColumn kResourceSummary[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400001u, 20, ColumnAlign::Left, "Resource Name"},
    {0x0c400003u, 10, ColumnAlign::Left, ""},  // Group
    {0x0c400004u, 10, ColumnAlign::Right, "Max. Units"},
    {0x0c40001au, 5, ColumnAlign::Right, ""},  // PeakUnits
    {0x0c400006u, 11, ColumnAlign::Right, "Std. Rate"},
    {0x0c400007u, 11, ColumnAlign::Right, "Ovt. Rate"},
    {0x0c40000cu, 14, ColumnAlign::Right, ""},  // Cost
    {0x0c40000du, 11, ColumnAlign::Right, ""},  // Work
};

constexpr RawColumn kResourceHyperlink[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400056u, 6, ColumnAlign::Left, "Indicators"},
    {0x0c400001u, 24, ColumnAlign::Left, "Resource Name"},
    {0x0c40008au, 17, ColumnAlign::Left, "Hyperlink"},
    {0x0c40008bu, 24, ColumnAlign::Left, "Address"},
    {0x0c40008cu, 17, ColumnAlign::Left, "SubAddress"},
};

constexpr RawColumn kResourceEarnedValue[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400001u, 24, ColumnAlign::Left, "Resource Name"},
    {0x0c400033u, 11, ColumnAlign::Right, ""},
    {0x0c400034u, 11, ColumnAlign::Right, ""},
    {0x0c400035u, 11, ColumnAlign::Right, ""},
    {0x0c400036u, 11, ColumnAlign::Right, ""},
    {0x0c40010cu, 11, ColumnAlign::Right, "ACWP"},
    {0x0c40000cu, 11, ColumnAlign::Right, ""},
    {0x0c400011u, 11, ColumnAlign::Right, "BAC"},
    {0x0c40012du, 11, ColumnAlign::Right, "EAC"},
};

constexpr RawColumn kResourceEntryWork[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400056u, 6, ColumnAlign::Left, "Indicators"},
    {0x0c400001u, 20, ColumnAlign::Left, "Resource Name"},
    {0x0c40012cu, 11, ColumnAlign::Left, ""},
    {0x0c4000fcu, 20, ColumnAlign::Left, ""},
    {0x0c400002u, 11, ColumnAlign::Left, ""},
    {0x0c400003u, 10, ColumnAlign::Left, ""},
    {0x0c400004u, 10, ColumnAlign::Right, "Max. Units"},
    {0x0c400006u, 11, ColumnAlign::Right, "Std. Rate"},
    {0x0c400007u, 11, ColumnAlign::Right, "Ovt. Rate"},
    {0x0c400012u, 11, ColumnAlign::Right, "Cost/Use"},
    {0x0c400013u, 10, ColumnAlign::Left, ""},
    {0x0c400005u, 14, ColumnAlign::Left, ""},
    {0x0c40000au, 13, ColumnAlign::Left, ""},
};

constexpr RawColumn kResourceEntryMaterial[] = {
    {0x0c400000u, 6, ColumnAlign::Right, ""},
    {0x0c400056u, 6, ColumnAlign::Left, "Indicators"},
    {0x0c400001u, 20, ColumnAlign::Left, "Resource Name"},
    {0x0c40012cu, 11, ColumnAlign::Left, ""},
    {0x0c40012bu, 13, ColumnAlign::Left, ""},
    {0x0c4000fcu, 20, ColumnAlign::Left, ""},
    {0x0c400002u, 11, ColumnAlign::Left, ""},
    {0x0c400003u, 10, ColumnAlign::Left, ""},
    {0x0c400006u, 11, ColumnAlign::Right, "Std. Rate"},
    {0x0c400012u, 11, ColumnAlign::Right, "Cost/Use"},
    {0x0c400013u, 10, ColumnAlign::Left, ""},
    {0x0c40000au, 13, ColumnAlign::Left, ""},
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

constexpr const char* kTaskEvidence =
    "evidence: PJINTL RT_RCDATA/330_1033.bin task table; field ids, widths and "
    "alignment codes measured (see memory/view.md). Captions are the resource's "
    "table captions; unnamed PjField ids take their title from analysis/typelib.tsv.";
constexpr const char* kResourceEvidence =
    "evidence: PJINTL RT_RCDATA/311_1033.bin resource table; field ids, widths and "
    "alignment codes measured (see memory/view.md). Captions are the resource's "
    "table captions; unnamed PjField ids take their title from analysis/typelib.tsv.";

TableDefinition make_table(std::string id, std::string name,
                           core::model::RecordType record_type, const RawColumn* columns,
                           std::size_t count, const char* evidence, int resource_table) {
    TableDefinition table;
    table.id = std::move(id);
    table.name = std::move(name);
    table.record_type = record_type;
    table.source = std::string(evidence) + " (resource table index " +
                   std::to_string(resource_table) + ")";
    table.columns.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Column column;
        column.field = columns[i].field;
        column.title = columns[i].title;
        column.width = columns[i].width;
        column.align = columns[i].align;
        table.columns.push_back(std::move(column));
    }
    return table;
}

template <std::size_t N>
TableDefinition make_task_table(std::string id, std::string name, const RawColumn (&columns)[N],
                                int resource_table) {
    return make_table(std::move(id), std::move(name), core::model::RecordType::Task, columns,
                      N, kTaskEvidence, resource_table);
}

template <std::size_t N>
TableDefinition make_resource_table(std::string id, std::string name,
                                    const RawColumn (&columns)[N], int resource_table) {
    return make_table(std::move(id), std::move(name), core::model::RecordType::Resource,
                      columns, N, kResourceEvidence, resource_table);
}

const std::vector<TableDefinition>& task_tables_impl() {
    static const std::vector<TableDefinition> kTables = {
        make_task_table("entry", "Entry", kTaskEntry, 0),
        make_task_table("variance", "Variance", kTaskVariance, 1),
        make_task_table("earned-value", "Earned Value", kTaskEarnedValue, 2),
        make_task_table("baseline", "Baseline", kTaskBaseline, 3),
        make_task_table("tracking", "Tracking", kTaskTracking, 4),
        make_task_table("schedule", "Schedule", kTaskSchedule, 5),
        make_task_table("work", "Work", kTaskWork, 6),
        make_task_table("cost", "Cost", kTaskCost, 7),
        make_task_table("summary", "Summary", kTaskSummary, 8),
        make_task_table("constraint-dates", "Constraint Dates", kTaskConstraintDates, 9),
        make_task_table("delay", "Delay", kTaskDelay, 10),
        make_task_table("usage", "Usage", kTaskUsage, 11),
        make_task_table("hyperlink", "Hyperlink", kTaskHyperlink, 12),
        make_task_table("earned-value-schedule-indicators",
                        "Earned Value Schedule Indicators", kTaskEarnedValueSchedule, 13),
        make_task_table("earned-value-cost-indicators", "Earned Value Cost Indicators",
                        kTaskEarnedValueCost, 14),
    };
    return kTables;
}

const std::vector<TableDefinition>& resource_tables_impl() {
    static const std::vector<TableDefinition> kTables = {
        make_resource_table("resource-entry", "Entry", kResourceEntry, 0),
        make_resource_table("resource-usage", "Usage", kResourceUsage, 1),
        make_resource_table("resource-cost", "Cost", kResourceCost, 2),
        make_resource_table("resource-work", "Work", kResourceWork, 3),
        make_resource_table("resource-summary", "Summary", kResourceSummary, 4),
        make_resource_table("resource-hyperlink", "Hyperlink", kResourceHyperlink, 5),
        make_resource_table("resource-earned-value", "Earned Value", kResourceEarnedValue, 7),
        make_resource_table("resource-entry-work-resources", "Entry - Work Resources",
                            kResourceEntryWork, 8),
        make_resource_table("resource-entry-material-resources", "Entry - Material Resources",
                            kResourceEntryMaterial, 9),
    };
    return kTables;
}

const std::vector<TableDefinition>& all_tables_impl() {
    static const std::vector<TableDefinition> kTables = [] {
        std::vector<TableDefinition> tables = task_tables_impl();
        const std::vector<TableDefinition>& resources = resource_tables_impl();
        tables.insert(tables.end(), resources.begin(), resources.end());
        return tables;
    }();
    return kTables;
}

}  // namespace

const std::vector<TableDefinition>& task_tables() { return task_tables_impl(); }

const std::vector<TableDefinition>& resource_tables() { return resource_tables_impl(); }

const std::vector<TableDefinition>& all_tables() { return all_tables_impl(); }

const TableDefinition* find_table(std::string_view id) noexcept {
    const std::vector<TableDefinition>& tables = all_tables_impl();
    for (const TableDefinition& table : tables) {
        if (table.id == id) {
            return &table;
        }
    }
    for (const TableDefinition& table : tables) {
        if (table.name == id) {
            return &table;
        }
    }
    return nullptr;
}

const TableDefinition* find_table(std::string_view name,
                                  core::model::RecordType record_type) noexcept {
    for (const TableDefinition& table : all_tables_impl()) {
        if (table.record_type == record_type && table.name == name) {
            return &table;
        }
    }
    return nullptr;
}

}  // namespace core::view::registry
