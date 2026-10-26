// tests/resource_view_tests.cpp -- W43: the resource RecordSet builder and the
// measured resource table set (PJINTL RT_RCDATA 311, decoded by W42).
//
// Covers: field ids and Value kinds, MaxUnits, Type (Work/Material/Cost, W40's
// core/mpp::ResourceType order), RecordSet identity, the empty case, and that
// every resource table column resolves through the descriptor table with the
// Resource record type.
#include <cstdint>
#include <string>
#include <vector>

#include "resource_tables.h"
#include "table_registry.h"
#include "view_test_support.h"

namespace {

using core::model::RecordType;
using core::view::Column;
using core::view::ColumnAlign;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::ResourceRecord;
using core::view::ResourceType;
using core::view::TableDefinition;
using core::view::ValueKind;
using view_test::TestContext;

constexpr FieldId kId = core::view::kResourceIdField;
constexpr FieldId kName = core::view::kResourceNameField;
constexpr FieldId kInitials = core::view::kResourceInitialsField;
constexpr FieldId kType = core::view::kResourceTypeField;
constexpr FieldId kMaxUnits = core::view::kResourceMaxUnitsField;
constexpr FieldId kMaterialLabel = core::view::kResourceMaterialLabelField;
constexpr FieldId kGroup = core::view::kResourceGroupField;
constexpr FieldId kStdRate = core::view::kResourceStandardRateField;
constexpr FieldId kOvtRate = core::view::kResourceOvertimeRateField;
constexpr FieldId kCostPerUse = core::view::kResourceCostPerUseField;
constexpr FieldId kAccrueAt = core::view::kResourceAccrueAtField;
constexpr FieldId kBaseCalendar = core::view::kResourceBaseCalendarField;
constexpr FieldId kCode = core::view::kResourceCodeField;
constexpr FieldId kPeakUnits = core::view::kResourcePeakUnitsField;
constexpr FieldId kOverallocated = core::view::kResourceOverallocatedField;
constexpr FieldId kIndicators = core::view::kResourceIndicatorsField;

ResourceRecord make_record(std::uint32_t uid, const std::string& name,
                           ResourceType type, double max_units) {
    ResourceRecord record;
    record.uid = uid;
    record.id = uid;
    record.name = name;
    record.initials = name.substr(0, 1);
    record.type = type;
    record.max_units_percent = max_units;
    return record;
}

void test_builder(TestContext& ctx) {
    std::vector<ResourceRecord> records;
    records.push_back(make_record(10, "Alpha", ResourceType::work, 100.0));
    records.push_back(make_record(11, "Beta", ResourceType::material, 50.0));
    records.push_back(make_record(12, "Gamma", ResourceType::cost, 0.0));
    // Alpha carries every stored Entry value; Beta is the MPP12 case (no
    // decoded rate words); Gamma has no custom values at all.
    records[0].group = "North";
    records[0].code = "CR-A";
    records[0].material_label = "";
    records[0].base_calendar = "Standard";
    records[0].standard_rate_per_hour = 50.0;
    records[0].overtime_rate_per_hour = 75.0;
    records[0].cost_per_use = 10.0;
    records[0].accrue_at = 3;
    records[0].cost_fields_stored = true;
    records[0].peak_units_percent = 300.0;
    records[0].overallocated = true;
    records[1].peak_units_percent = 50.0;

    const RecordSet set = core::view::build_resource_record_set(records);
    ctx.check(set.record_type() == RecordType::Resource, "builder record type is Resource");
    ctx.equal(set.row_count(), std::size_t{3}, "three resource rows");
    ctx.equal(set.field_count(), std::size_t{15}, "fifteen builder fields");
    ctx.check(set.schema().size() == 15, "schema has fifteen entries");
    if (set.schema().size() == 15) {
        // The stored Entry order without Indicators, then the computed fields.
        ctx.equal(set.schema()[0], kId, "schema[0] is ID");
        ctx.equal(set.schema()[1], kName, "schema[1] is Name");
        ctx.equal(set.schema()[2], kType, "schema[2] is Type");
        ctx.equal(set.schema()[3], kMaterialLabel, "schema[3] is Material Label");
        ctx.equal(set.schema()[4], kInitials, "schema[4] is Initials");
        ctx.equal(set.schema()[5], kGroup, "schema[5] is Group");
        ctx.equal(set.schema()[6], kMaxUnits, "schema[6] is MaxUnits");
        ctx.equal(set.schema()[7], kStdRate, "schema[7] is Std. Rate");
        ctx.equal(set.schema()[8], kOvtRate, "schema[8] is Ovt. Rate");
        ctx.equal(set.schema()[9], kCostPerUse, "schema[9] is Cost/Use");
        ctx.equal(set.schema()[10], kAccrueAt, "schema[10] is Accrue At");
        ctx.equal(set.schema()[11], kBaseCalendar, "schema[11] is Base Calendar");
        ctx.equal(set.schema()[12], kCode, "schema[12] is Code");
        ctx.equal(set.schema()[13], kPeakUnits, "schema[13] is PeakUnits");
        ctx.equal(set.schema()[14], kOverallocated, "schema[14] is Overallocated");
    }
    ctx.equal(set.column_of(kMaxUnits), 6, "column_of(MaxUnits)");
    ctx.equal(set.row_of_uid(11), 1, "row_of_uid(11)");
    ctx.equal(set.row_of_uid(99), -1, "row_of_uid(unknown)");
    ctx.equal(set.uid(0), 10u, "row 0 uid");
    ctx.equal(set.uid(1), 11u, "row 1 uid");
    ctx.equal(set.uid(2), 12u, "row 2 uid");

    const core::view::Value* id = set.get(0, kId);
    ctx.check(id != nullptr && id->kind == ValueKind::Integer, "ID is an Integer");
    if (id != nullptr) {
        ctx.equal(id->integer, std::int64_t{10}, "ID value");
    }
    const core::view::Value* name = set.get(0, kName);
    ctx.check(name != nullptr && name->kind == ValueKind::Text, "Name is Text");
    if (name != nullptr) {
        ctx.equal_string(name->text, "Alpha", "Name value");
    }
    const core::view::Value* initials = set.get(0, kInitials);
    ctx.check(initials != nullptr && initials->kind == ValueKind::Text,
              "Initials is Text");
    if (initials != nullptr) {
        ctx.equal_string(initials->text, "A", "Initials value");
    }
    const core::view::Value* type = set.get(0, kType);
    ctx.check(type != nullptr && type->kind == ValueKind::Integer, "Type is an Integer");
    if (type != nullptr) {
        ctx.equal(type->integer, std::int64_t{0}, "Work type is 0");
    }
    const core::view::Value* units = set.get(0, kMaxUnits);
    ctx.check(units != nullptr && units->kind == ValueKind::Number,
              "MaxUnits is a Number");
    if (units != nullptr) {
        ctx.equal(units->number, 100.0, "MaxUnits percent");
    }
    ctx.equal(set.get(1, kType)->integer, std::int64_t{1}, "Material type is 1");
    ctx.equal(set.get(2, kType)->integer, std::int64_t{2}, "Cost type is 2");
    ctx.equal(set.get(1, kMaxUnits)->number, 50.0, "second MaxUnits");
    ctx.equal(set.get(2, kMaxUnits)->number, 0.0, "third MaxUnits");

    // The stored text fields.
    ctx.check(set.get(0, kGroup) != nullptr &&
                  set.get(0, kGroup)->kind == ValueKind::Text &&
                  set.get(0, kGroup)->text == "North",
              "Group is the stored text");
    ctx.equal_string(set.get(0, kCode)->text, "CR-A", "Code value");

    ctx.equal_string(set.get(0, kBaseCalendar)->text, "Standard", "Base Calendar value");
    ctx.check(set.get(0, kMaterialLabel) != nullptr &&
                  set.get(0, kMaterialLabel)->kind == ValueKind::Text,
              "an empty Material Label is a stored empty Text");
    // Accrue At stays the PjAccrueAt integer; the app maps it to a word.
    ctx.check(set.get(0, kAccrueAt) != nullptr &&
                  set.get(0, kAccrueAt)->kind == ValueKind::Integer &&
                  set.get(0, kAccrueAt)->integer == 3,
              "Accrue At is the stored integer");
    // The rates and Cost/Use are numbers in reader units when stored.
    ctx.check(set.get(0, kStdRate) != nullptr &&
                  set.get(0, kStdRate)->kind == ValueKind::Number &&
                  set.get(0, kStdRate)->number == 50.0,
              "Standard Rate is the decoded per-hour number");
    ctx.equal(set.get(0, kOvtRate)->number, 75.0, "Overtime Rate value");
    ctx.equal(set.get(0, kCostPerUse)->number, 10.0, "Cost/Use value");
    // Beta's row has no decoded rate words: the values exist and are Null, not
    // an invented zero a renderer would print as "$0.00".
    for (const FieldId field : {kStdRate, kOvtRate, kCostPerUse}) {
        const core::view::Value* value = set.get(1, field);
        ctx.check(value != nullptr && value->kind == ValueKind::Null,
                  "MPP12 rate/cost values are Null");
    }
    // The computed load fields are passed through as stored by the caller.
    ctx.check(set.get(0, kPeakUnits) != nullptr &&
                  set.get(0, kPeakUnits)->kind == ValueKind::Number &&
                  set.get(0, kPeakUnits)->number == 300.0,
              "PeakUnits is a Number in percent");
    ctx.check(set.get(0, kOverallocated) != nullptr &&
                  set.get(0, kOverallocated)->kind == ValueKind::Bool &&
                  set.get(0, kOverallocated)->boolean,
              "Overallocated is a Bool");
    ctx.check(set.get(1, kOverallocated) != nullptr &&
                  !set.get(1, kOverallocated)->boolean,
              "a load within MaxUnits is not overallocated");

    // Indicators is deliberately not a builder field: its value is the status
    // glyph, and a table column for it renders from the overallocated flag.
    ctx.check(set.get(0, kIndicators) == nullptr,
              "Indicators is not a builder field");

    const RecordSet empty = core::view::build_resource_record_set({});
    ctx.equal(empty.row_count(), std::size_t{0}, "empty input has no rows");
    ctx.equal(empty.field_count(), std::size_t{15}, "empty input keeps the schema");
    ctx.check(empty.empty(), "empty RecordSet reports empty()");
}

void test_field_kinds(TestContext& ctx) {
    ctx.check(core::view::resource_field_kind(kId) == ValueKind::Integer,
              "ID kind Integer");
    ctx.check(core::view::resource_field_kind(core::view::kResourceUniqueIdField) ==
                  ValueKind::Integer,
              "UniqueID kind Integer");
    ctx.check(core::view::resource_field_kind(kName) == ValueKind::Text,
              "Name kind Text");
    ctx.check(core::view::resource_field_kind(kInitials) == ValueKind::Text,
              "Initials kind Text");
    ctx.check(core::view::resource_field_kind(kType) == ValueKind::Integer,
              "Type kind Integer");
    ctx.check(core::view::resource_field_kind(kMaxUnits) == ValueKind::Number,
              "MaxUnits kind Number (view kind, not descriptor i64)");
    ctx.check(core::view::resource_field_kind(kGroup) == ValueKind::Text,
              "Group kind Text");
    ctx.check(core::view::resource_field_kind(kCode) == ValueKind::Text,
              "Code kind Text");
    ctx.check(core::view::resource_field_kind(kMaterialLabel) == ValueKind::Text,
              "Material Label kind Text");
    ctx.check(core::view::resource_field_kind(kBaseCalendar) == ValueKind::Text,
              "Base Calendar kind Text");
    ctx.check(core::view::resource_field_kind(kStdRate) == ValueKind::Number,
              "Standard Rate kind Number");
    ctx.check(core::view::resource_field_kind(kOvtRate) == ValueKind::Number,
              "Overtime Rate kind Number");
    ctx.check(core::view::resource_field_kind(kCostPerUse) == ValueKind::Number,
              "Cost/Use kind Number");
    ctx.check(core::view::resource_field_kind(kAccrueAt) == ValueKind::Integer,
              "Accrue At kind Integer");
    ctx.check(core::view::resource_field_kind(kPeakUnits) == ValueKind::Number,
              "PeakUnits kind Number");
    ctx.check(core::view::resource_field_kind(kOverallocated) == ValueKind::Bool,
              "Overallocated kind Bool");
    ctx.check(core::view::resource_field_kind(kIndicators) == ValueKind::Null,
              "Indicators has no builder value kind");
    ctx.check(core::view::resource_field_kind(0x0c4000feu) == ValueKind::Null,
              "unknown resource field kind Null");

    ctx.equal_string(core::view::resource_type_name(ResourceType::work), "Work",
                     "work name");
    ctx.equal_string(core::view::resource_type_name(ResourceType::material), "Material",
                     "material name");
    ctx.equal_string(core::view::resource_type_name(ResourceType::cost), "Cost",
                     "cost name");
}

void test_resource_tables(TestContext& ctx) {
    const std::vector<TableDefinition>& tables = core::view::resource_table_definitions();
    ctx.equal(tables.size(), std::size_t{9}, "nine resource tables");

    std::string error = "stale";
    ctx.check(core::view::resource_tables_resolve(&error),
              "every resource table column resolves through the descriptor table");
    ctx.check(error.empty(), "no resolution error was reported");

    const char* const expected_ids[] = {
        "resource-entry",
        "resource-usage",
        "resource-cost",
        "resource-work",
        "resource-summary",
        "resource-hyperlink",
        "resource-earned-value",
        "resource-entry-work-resources",
        "resource-entry-material-resources",
    };
    const std::size_t expected_columns[] = {15, 4, 7, 9, 9, 6, 10, 14, 12};
    std::size_t total_columns = 0;
    for (std::size_t i = 0; i < tables.size() && i < 9; ++i) {
        const TableDefinition& table = tables[i];
        ctx.equal_string(table.id, expected_ids[i], "resource table id");
        ctx.check(table.record_type == RecordType::Resource,
                  "resource table record type is Resource");
        ctx.equal(table.columns.size(), expected_columns[i], "resource table column count");
        total_columns += table.columns.size();
        ctx.check(table.source.find("311") != std::string::npos,
                  "resource table source names RCDATA 311");
        for (const Column& column : table.columns) {
            const core::model::FieldInfo* info =
                core::model::find_field_by_id(column.field);
            ctx.check(info != nullptr, "column field is in the descriptor table");
            if (info != nullptr) {
                ctx.check(info->record_type ==
                              static_cast<std::uint8_t>(RecordType::Resource),
                          "column field is a Resource field");
            }
            ctx.check(!column.display_title(RecordType::Resource).empty(),
                      "column title is non-empty");
        }
    }
    ctx.equal(total_columns, std::size_t{86}, "resource tables carry 86 columns");

    const TableDefinition* entry = core::view::find_resource_table("resource-entry");
    ctx.check(entry != nullptr, "find_resource_table by id");
    if (entry != nullptr) {
        ctx.equal_string(entry->name, "Entry", "resource entry display name");
        ctx.check(entry->has(kId), "Entry has ID");
        ctx.check(entry->has(kType), "Entry has Type");
        ctx.check(entry->has(kInitials), "Entry has Initials");
        ctx.check(entry->has(kMaxUnits), "Entry has MaxUnits");
        ctx.check(!entry->has(core::view::kResourceUniqueIdField),
                  "Entry has no UniqueID column");
    }
    const TableDefinition* by_name = core::view::find_resource_table("Entry");
    ctx.check(by_name == entry, "find_resource_table by name returns the same table");
    const TableDefinition* usage = core::view::find_resource_table("Usage");
    ctx.check(usage != nullptr && usage->id == "resource-usage",
              "Usage resolves to the resource Usage table");
    ctx.check(core::view::find_resource_table("entry") == nullptr,
              "lower-case 'entry' does not match a resource table name");
    ctx.check(core::view::find_resource_table("nope") == nullptr,
              "unknown resource table is null");

    // Pin the measured Entry-column data the builder feeds (IDs/widths/align).
    if (entry != nullptr) {
        const Column* units = entry->find(kMaxUnits);
        ctx.check(units != nullptr, "Entry has a MaxUnits column");
        if (units != nullptr) {
            ctx.equal(units->width, 10, "MaxUnits width is 10");
            ctx.check(units->align == ColumnAlign::Right, "MaxUnits is right aligned");
            ctx.equal_string(units->display_title(RecordType::Resource), "Max. Units",
                             "MaxUnits caption");
        }
        const Column* type = entry->find(kType);
        ctx.check(type != nullptr, "Entry has a Type column");
        if (type != nullptr) {
            ctx.equal(type->width, 11, "Type width is 11");
            ctx.check(type->align == ColumnAlign::Left, "Type is left aligned");
            ctx.equal_string(type->display_title(RecordType::Resource), "Type",
                             "Type caption");
        }
        const Column* id = entry->find(kId);
        ctx.check(id != nullptr, "Entry has an ID column");
        if (id != nullptr) {
            ctx.equal(id->width, 6, "ID width is 6");
            ctx.check(id->align == ColumnAlign::Right, "ID is right aligned");
        }
        const Column* name = entry->find(kName);
        ctx.check(name != nullptr, "Entry has a Name column");
        if (name != nullptr) {
            ctx.equal(name->width, 20, "Name width is 20");
            ctx.equal_string(name->display_title(RecordType::Resource), "Resource Name",
                             "Name caption");
        }
    }

    // The table registry still returns the same nine objects; the wrapper is
    // additive and does not duplicate or retitle them.
    ctx.check(&core::view::registry::resource_tables() ==
                  &core::view::resource_table_definitions(),
              "resource_table_definitions aliases the registry vector");
    ctx.equal(core::view::registry::task_tables().size(), std::size_t{15},
              "task tables are untouched (15)");
}

void test_table_columns_against_builder(TestContext& ctx) {
    // Feed the builder's RecordSet through the resource Entry table: every
    // builder field the table also names must read the same value.
    std::vector<ResourceRecord> records;
    records.push_back(make_record(21, "Builder", ResourceType::material, 75.0));
    const RecordSet set = core::view::build_resource_record_set(records);
    const TableDefinition* entry = core::view::find_resource_table("resource-entry");
    ctx.check(entry != nullptr, "resource Entry table is available");
    if (entry == nullptr) {
        return;
    }
    int resolved = 0;
    for (const Column& column : entry->columns) {
        if (!set.has_field(column.field)) {
            continue;
        }
        ++resolved;
        const core::view::Value* value = set.get(0, column.field);
        ctx.check(value != nullptr, "table column resolves to a RecordSet value");
    }
    // The registry's Entry table has 15 columns; the builder supplies every
    // stored one it models (13) and deliberately leaves Indicators and
    // Phonetics absent.
    ctx.equal(resolved, 13, "thirteen Entry columns are supplied by the builder");
    // The other Entry columns are not fabricated by the builder; a table read
    // returns null and renders blank, which is the documented RecordSet rule.
    bool saw_absent = false;
    for (const Column& column : entry->columns) {
        if (!set.has_field(column.field)) {
            saw_absent = true;
            ctx.check(set.get(0, column.field) == nullptr,
                      "absent column reads null, not a fabricated value");
        }
    }
    ctx.check(saw_absent, "the Entry table has columns the builder does not supply");
    ctx.check(!set.has_field(kIndicators) && !set.has_field(0x0c4000fcu),
              "Indicators and Phonetics are the absent Entry columns");
}

}  // namespace

int main() {
    TestContext ctx;
    test_builder(ctx);
    test_field_kinds(ctx);
    test_resource_tables(ctx);
    test_table_columns_against_builder(ctx);
    return ctx.finish("resource_view_tests");
}
