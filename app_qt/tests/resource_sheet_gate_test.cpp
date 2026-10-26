// app_qt/tests/resource_sheet_gate_test.cpp -- wave 31, G3: the Resource
// Sheet's own stored presentation, read through the real PlanDocument.
//
// W31-3.4 (this commit) proves the app seam that the later fidelity work
// builds on:
//   * PlanDocument keeps the file's RESOURCE tables separately from the task
//     tables and selects the one the Resource Sheet ViewDef names in
//     0x2240000a ("&Entry");
//   * that table is the measured 14-column Entry table, in file order, with
//     each column's stored character width;
//   * the view's 1,530-byte style record decodes to the 19 Text Styles slots,
//     including slot 2 (ordinary resource) and slot 3 (overallocated, bold
//     RGB 165,0,0).
//
// W31-3.5..3.7 extend this file with the model values and the live Qt gate
// measured against Project's COM facts on the same committed fixture
// (tests/data/wave31-resource-sheet.mpp), so every number here is Project's.
//
// Usage: qt_resource_sheet_gate_tests <wave31-resource-sheet.mpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "plan_document.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

struct StoredColumn {
    std::uint32_t field;
    int chars;
};

// The stored Resource Entry table as Project saved it on the fixture: field id
// and the character width in the record's first opaque header byte.  COM's
// TableFields reports the same 14 visible fields; its px widths follow the
// sheet font (Segoe UI 10 -> 6 px/char, +3 px border), which W31-3.6 renders.
const StoredColumn kResourceEntryColumns[] = {
    {0x0c400000u, 6},   // ID
    {0x0c400056u, 6},   // Indicators
    {0x0c400001u, 20},  // Resource Name
    {0x0c40012cu, 11},  // Type
    {0x0c40012bu, 13},  // Material Label
    {0x0c400002u, 11},  // Initials
    {0x0c400003u, 10},  // Group
    {0x0c400004u, 10},  // Max. Units
    {0x0c400006u, 11},  // Std. Rate
    {0x0c400007u, 11},  // Ovt. Rate
    {0x0c400012u, 11},  // Cost/Use
    {0x0c400013u, 10},  // Accrue At
    {0x0c400005u, 14},  // Base Calendar
    {0x0c40000au, 13},  // Code
};

void check_stored_table(const msqt::PlanDocument& document) {
    const msproject::viewstate::TableDef& table = document.resource_table();
    check(table.decoded, "resource gate: the stored resource table decoded");
    check(table.columns.size() == std::size(kResourceEntryColumns),
          "resource gate: the stored Resource Entry table has 14 columns");
    if (table.columns.size() != std::size(kResourceEntryColumns)) {
        return;
    }
    for (std::size_t index = 0; index < table.columns.size(); ++index) {
        const msproject::viewstate::ColumnDef& column = table.columns[index];
        check(column.field_id == kResourceEntryColumns[index].field,
              "resource gate: stored field id matches the measured Entry table");
        check(static_cast<int>(column.opaque_header[0]) ==
                  kResourceEntryColumns[index].chars,
              "resource gate: stored character width matches the measured Entry table");
    }
}

void check_resource_sheet_style(const msqt::PlanDocument& document) {
    const app::ResourceSheetStyle& style = document.resource_sheet_style();
    check(style.decoded, "resource gate: the Resource Sheet view decoded");
    check(style.view_name == "Resource &Sheet",
          "resource gate: the stored view is 'Resource &Sheet'");
    check(style.display_name == "Resource Sheet",
          "resource gate: the stored display name is 'Resource Sheet'");
    check(style.table_name == "&Entry",
          "resource gate: the view names the '&Entry' table");
    check(style.filter_name == "&All Resources",
          "resource gate: the view names the '&All Resources' filter");
    check(style.text.decoded && style.text.text.size() ==
                                    msproject::viewstate::kTextStyleItemCount,
          "resource gate: the view's 19 text styles decoded");

    const auto& normal = style.text.text[static_cast<std::size_t>(
        msproject::viewstate::TextStyleItem::non_critical)];
    check(normal.valid && normal.font_name == "Calibri" && normal.point_size == 11 &&
              !normal.bold && !normal.has_colour && !normal.has_background,
          "resource gate: slot 2 is automatic upright Calibri 11");
    const auto& overallocated = style.text.text[static_cast<std::size_t>(
        msproject::viewstate::TextStyleItem::critical)];
    check(overallocated.valid && overallocated.font_name == "Calibri" &&
              overallocated.point_size == 11 && overallocated.bold,
          "resource gate: slot 3 is bold Calibri 11");
    check(overallocated.has_colour &&
              overallocated.colour == (msproject::viewstate::Rgb{165, 0, 0}),
          "resource gate: slot 3 is the stored RGB 165,0,0");
    check(!overallocated.has_background,
          "resource gate: slot 3's background is automatic");
}

void check_resources(const msqt::PlanDocument& document) {
    const std::vector<app::ResourceRow>& resources = document.plan().resources;
    check(resources.size() == 3,
          "resource gate: the fixture has three live resources");
    if (resources.size() != 3) {
        return;
    }
    const char* const names[] = {"Crew A", "Crew B", "Crew C"};
    for (std::size_t index = 0; index < resources.size(); ++index) {
        check(resources[index].id == index + 1,
              "resource gate: resource IDs follow the stored order");
        check(resources[index].name == names[index],
              "resource gate: resource names follow the stored order");
    }
    // COM's Resource.MaxUnits is 1.0 on all three rows; the 300 % crew A
    // reaches is PeakUnits, computed from its three overlapping assignments
    // (W31-3.5 makes the port compute the same number).
    for (const app::ResourceRow& resource : resources) {
        check(resource.max_units_percent == 100.0,
              "resource gate: every stored Max. Units is 100 %");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: qt_resource_sheet_gate_tests "
                     "<wave31-resource-sheet.mpp>\n");
        return 2;
    }
    msqt::PlanDocument document;
    std::string error;
    if (!document.load(argv[1], app::ZoomLevel::week, error)) {
        std::fprintf(stderr, "FAIL: cannot load the fixture: %s\n", error.c_str());
        return 1;
    }

    check(document.resource_table_count() >= 1,
          "resource gate: the file stores at least one resource table");
    check(!document.table().columns.empty(),
          "resource gate: the task sheet still has its task table");
    for (const msproject::viewstate::ColumnDef& column : document.table().columns) {
        check((column.field_id & 0xffff0000u) != 0x0c400000u,
              "resource gate: no resource column leaks into the task table");
    }
    check_stored_table(document);
    check_resource_sheet_style(document);
    check_resources(document);

    std::printf("qt_resource_sheet_gate: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
