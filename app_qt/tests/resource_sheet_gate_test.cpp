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
// W31-3.5 checks the model's stored values; W31-3.6 checks the Qt adapter's
// gutter, text styles and painted Indicators glyphs; W31-3.7 opens the real
// MainWindow on the same committed fixture (tests/data/wave31-resource-sheet.mpp)
// and pins the LIVE Resource Sheet against Project's COM facts: the visible
// Entry fields in COM order, Project's chars * unit + 3 pixel widths from the
// view's own font unit, the frozen gutter's stored IDs and every resource row's
// projected values.  Every number here is Project's.
//
// Usage: qt_resource_sheet_gate_tests <wave31-resource-sheet.mpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QImage>
#include <QModelIndex>
#include <QPoint>
#include <QTableView>
#include <QVariant>

#include "entry_columns.h"
#include "main_window.h"
#include "plan_document.h"
#include "shell_style.h"
#include "sheet.h"
#include "sheet_table_model.h"
#include "sheet_views.h"
#include "view_model.h"

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
    // reaches is PeakUnits, computed from its three overlapping assignments.
    for (const app::ResourceRow& resource : resources) {
        check(resource.max_units_percent == 100.0,
              "resource gate: every stored Max. Units is 100 %");
    }
    // W31-3.5: the stored Entry values the reader decoded (COM facts in
    // build/w31/W31-G3-resource-sheet.json and W31-G3-probe.json).
    check(resources[0].group == "North" && resources[0].code == "CR-A" &&
              resources[0].base_calendar == "Standard" &&
              resources[0].accrue_at == 3 && resources[0].cost_fields_stored,
          "resource gate: Crew A carries its stored Entry text/accrual");
    check(resources[0].standard_rate_per_hour == 0.0 &&
              resources[0].overtime_rate_per_hour == 0.0 &&
              resources[0].cost_per_use == 0.0,
          "resource gate: Crew A's stored rates are zero");
    check(resources[1].standard_rate_per_hour == 50.0 &&
              resources[1].overtime_rate_per_hour == 75.0 &&
              resources[1].cost_per_use == 10.0,
          "resource gate: Crew B's stored rates are 50/75 per hour, 10 per use");
    check(resources[1].accrue_at == 1 && resources[2].accrue_at == 2,
          "resource gate: Crew B accrues at Start, Crew C at End");
    check(resources[1].group == "South" && resources[2].group == "North",
          "resource gate: groups follow the stored text");
}

// W31-3.5: the app model renders the stored table's 14 columns, its measured
// captions/alignments, and Project's cell texts, with Crew A overallocated.
void check_resource_model(const msqt::PlanDocument& document) {
    const std::vector<core::sheet::SheetColumn> columns =
        app::resource_sheet_columns(document.resource_table());
    check(columns.size() == std::size(kResourceEntryColumns),
          "resource gate: the model uses all 14 stored columns");
    if (columns.size() != std::size(kResourceEntryColumns)) {
        return;
    }
    for (std::size_t index = 0; index < columns.size(); ++index) {
        check(columns[index].field == kResourceEntryColumns[index].field,
              "resource gate: model column field follows file order");
        check(columns[index].width == kResourceEntryColumns[index].chars,
              "resource gate: model column keeps the stored width");
        check(columns[index].read_only,
              "resource gate: the resource sheet is read-only");
    }
    check(!columns[0].visible, "resource gate: the ID column is the row-number gutter");
    check(columns[1].visible && columns[1].title.empty(),
          "resource gate: Indicators is visible with no caption text");
    check(columns[2].title == "Resource Name" &&
              columns[7].title == "Max. Units" &&
              columns[8].title == "Std. Rate" && columns[9].title == "Ovt. Rate" &&
              columns[10].title == "Cost/Use",
          "resource gate: the stored table's titles are kept");

    const core::view::RecordSet records =
        app::make_resource_view_records(document.plan(), nullptr);
    check(records.row_count() == 3, "resource gate: the model has three rows");
    if (records.row_count() != 3) {
        return;
    }
    const auto text_of = [&records](std::size_t row, core::view::FieldId field,
                                    const char* want, const char* what) {
        const core::view::Value* value = records.get(row, field);
        check(value != nullptr && value->kind == core::view::ValueKind::Text &&
                  value->text == want,
              what);
    };
    text_of(0, core::view::kResourceTypeField, "Work",
            "resource gate: Type renders Work");
    text_of(0, core::view::kResourceGroupField, "North",
            "resource gate: Crew A group text");
    text_of(0, core::view::kResourceStandardRateField, "$0.00/hr",
            "resource gate: Crew A standard rate renders $0.00/hr");
    text_of(0, core::view::kResourceOvertimeRateField, "$0.00/hr",
            "resource gate: Crew A overtime rate renders $0.00/hr");
    text_of(0, core::view::kResourceCostPerUseField, "$0.00",
            "resource gate: Crew A cost/use renders $0.00");
    text_of(0, core::view::kResourceAccrueAtField, "Prorated",
            "resource gate: Crew A accrues Prorated");
    text_of(1, core::view::kResourceStandardRateField, "$50.00/hr",
            "resource gate: Crew B standard rate renders $50.00/hr");
    text_of(1, core::view::kResourceOvertimeRateField, "$75.00/hr",
            "resource gate: Crew B overtime rate renders $75.00/hr");
    text_of(1, core::view::kResourceCostPerUseField, "$10.00",
            "resource gate: Crew B cost/use renders $10.00");
    text_of(1, core::view::kResourceAccrueAtField, "Start",
            "resource gate: Crew B accrues at Start");
    text_of(2, core::view::kResourceAccrueAtField, "End",
            "resource gate: Crew C accrues at End");
    const core::view::Value* peak = records.get(0, core::view::kResourcePeakUnitsField);
    check(peak != nullptr && peak->kind == core::view::ValueKind::Number &&
              peak->number == 300.0,
          "resource gate: Crew A PeakUnits is 300 %");
    const core::view::Value* overallocated =
        records.get(0, core::view::kResourceOverallocatedField);
    check(overallocated != nullptr && overallocated->boolean,
          "resource gate: Crew A is overallocated");
    const core::view::Value* normal =
        records.get(1, core::view::kResourceOverallocatedField);
    check(normal != nullptr && !normal->boolean,
          "resource gate: Crew B is not overallocated");
}

// W31-3.6: the adapter paints Project's Resource Sheet -- the stored ID in the
// frozen gutter, slot 3's bold red on the overallocated row against slot 2's
// upright automatic text on the others, and the Indicators column's (i) header
// badge plus the red overallocation glyph.  W31-3.7 then pins the same facts in
// the real window, including the widths this standalone view does not set.
void check_qt_model(const msqt::PlanDocument& document) {
    core::view::RecordSet records =
        app::make_resource_view_records(document.plan(), nullptr);
    core::sheet::SheetModel sheet(
        records, app::resource_sheet_columns(document.resource_table()));
    msqt::SheetTableModel model;
    model.set_sheet(&sheet);
    model.set_generic(true);
    model.set_resource_style(&document.resource_sheet_style().text);

    check(model.columnCount() == 13,
          "resource gate: the Qt model shows the 13 stored scrolling columns");
    if (model.columnCount() != 13) {
        return;
    }
    check(model.indicators_column() == 0,
          "resource gate: Indicators is the first scrolling column");
    check(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty(),
          "resource gate: the Qt header's Indicators column is captionless");
    check(model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString() ==
              QStringLiteral("Resource Name"),
          "resource gate: the Qt header carries the stored Resource Name caption");

    check(model.rowCount() == 3, "resource gate: the Qt model has the three resources");
    if (model.rowCount() != 3) {
        return;
    }
    check(model.row_number_text(0) == QStringLiteral("1") &&
              model.row_number_text(1) == QStringLiteral("2") &&
              model.row_number_text(2) == QStringLiteral("3"),
          "resource gate: the frozen gutter reads the stored Resource.ID");
    check(model.row_overallocated(0) && !model.row_overallocated(1) &&
              !model.row_overallocated(2),
          "resource gate: only Crew A carries the overallocated flag");
    check(model.index(0, 1).data(Qt::DisplayRole).toString() == QStringLiteral("Crew A"),
          "resource gate: the Qt model shows the stored resource name");

    // Slot 3 (overallocated) vs slot 2 (ordinary), from the view's own record.
    const QFont overallocated_font = model.index(0, 1).data(Qt::FontRole).value<QFont>();
    check(overallocated_font.bold() && overallocated_font.pointSize() == 11 &&
              overallocated_font.family() == QStringLiteral("Calibri"),
          "resource gate: Crew A paints slot 3's bold Calibri 11");
    const QColor overallocated_colour =
        model.index(0, 1).data(Qt::ForegroundRole).value<QColor>();
    check(overallocated_colour.isValid() && overallocated_colour.red() == 165 &&
              overallocated_colour.green() == 0 && overallocated_colour.blue() == 0,
          "resource gate: Crew A paints the stored RGB 165,0,0");
    const QFont normal_font = model.index(1, 1).data(Qt::FontRole).value<QFont>();
    check(!normal_font.bold() && normal_font.pointSize() == 11,
          "resource gate: Crew B paints slot 2's upright Calibri 11");
    check(!model.index(1, 1).data(Qt::ForegroundRole).isValid(),
          "resource gate: Crew B paints the automatic colour");

    // The painted chrome: the 16 px (i) badge in the captionless header and the
    // red person glyph on the overallocated row only.
    QTableView view;
    view.setModel(&model);
    auto* delegate = new msqt::EntryItemDelegate(&view);
    delegate->set_entry_mode(false);
    view.setItemDelegate(delegate);
    auto* header = new msqt::SheetHeaderView(Qt::Horizontal, &view);
    header->set_model_titles(true);
    view.setHorizontalHeader(header);
    view.verticalHeader()->setDefaultSectionSize(22);
    const int indicators_width = msqt::column_width_px(6, msqt::kColumnCharPx);
    view.setColumnWidth(0, indicators_width);
    view.resize(500, 3 * 22 + 10);
    view.show();
    QApplication::processEvents();

    const QImage header_image = header->grab().toImage();
    const int section_center =
        header->sectionViewportPosition(0) + header->sectionSize(0) / 2;
    const QColor badge = header_image.pixelColor(section_center - 5, header->height() / 2);
    check(badge.isValid() && badge.blue() > 150 && badge.blue() > badge.red() + 50,
          "resource gate: the Indicators header paints the measured blue (i) badge");

    const QImage body = view.viewport()->grab().toImage();
    const QColor glyph = body.pixelColor(indicators_width / 2, 6);
    check(glyph.isValid() && glyph.red() > 150 && glyph.green() < 130 &&
              glyph.blue() < 130,
          "resource gate: the overallocated row paints the red indicator glyph");
    const QColor blank = body.pixelColor(indicators_width / 2, 22 + 6);
    check(!(blank.isValid() && blank.red() > 150 && blank.green() < 130 &&
            blank.blue() < 130),
          "resource gate: an ordinary row's Indicators cell has no red glyph");
}

// W31-3.7: the live gate.  Open the committed fixture in the real MainWindow,
// switch to the Resource Sheet through its own menu action, and read the live
// widget tree back against build/w31/W31-G3-probe.json:
//   * COM TableFields lists the 14 stored fields (ID, Indicators, Resource
//     Name, Type, Material Label, Initials, Group, Max. Units, Std. Rate, Ovt.
//     Rate, Cost/Use, Accrue At, Base Calendar, Code) plus a -1 stub; the live
//     sheet shows the other 13 in that order with ID in the frozen gutter.
//   * COM reports each field's width in characters.  Project paints chars *
//     unit + 3 px, and this view's FONT_BASES[0] is Segoe UI 10, measured at
//     6 px/char (W31-G3-fonts.log), giving 39/123/69/81/69/63/63/69/69/69/
//     63/87/81.
//   * COM's Resource values are the three crews' stored rows with Crew A
//     overallocated (PeakUnits 3.0); the live cells must render exactly those
//     values, IDs and slot-2/slot-3 styles.
QAction* find_action(msqt::MainWindow& window, app::MenuAction id) {
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->data().isValid() && action->data().toInt() == static_cast<int>(id)) {
            return action;
        }
    }
    return nullptr;
}

void check_live_window(const std::string& fixture) {
    msqt::configure_application();
    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    check(window.open_file(QString::fromStdString(fixture), &error),
          "resource gate: the live window opens the fixture");
    window.show();
    QApplication::processEvents();

    QAction* resource_sheet =
        find_action(window, app::MenuAction::view_resource_sheet);
    check(resource_sheet != nullptr, "resource gate: the View menu carries Resource Sheet");
    if (resource_sheet == nullptr) {
        return;
    }
    resource_sheet->trigger();
    QApplication::processEvents();
    check(window.view_mode() == app::ViewMode::resource_sheet,
          "resource gate: the live window switches to the Resource Sheet");
    QTableView* sheet = window.sheet_view();
    check(sheet != nullptr && sheet->model() != nullptr,
          "resource gate: the live window has a sheet model");
    if (sheet == nullptr || sheet->model() == nullptr) {
        return;
    }
    auto* model = dynamic_cast<msqt::SheetTableModel*>(sheet->model());
    check(model != nullptr && model->sheet() != nullptr,
          "resource gate: the live window uses the resource sheet adapter");
    if (model == nullptr || model->sheet() == nullptr) {
        return;
    }

    // COM's visible fields, minus ID (Project draws it as the frozen gutter)
    // and the Add New Column stub (-1), which this port does not draw.
    const core::sheet::SheetModel& stored = *model->sheet();
    check(model->columnCount() == 13 &&
              stored.visible_column_count() == 13,
          "resource gate: the live Resource Sheet shows the 13 scrolling COM fields");
    if (model->columnCount() != 13) {
        return;
    }
    const char* const titles[] = {"",         "Resource Name", "Type",
                                  "Material Label", "Initials", "Group",
                                  "Max. Units", "Std. Rate", "Ovt. Rate",
                                  "Cost/Use", "Accrue At", "Base Calendar",
                                  "Code"};
    for (int ordinal = 0; ordinal < 13; ++ordinal) {
        const std::size_t column =
            stored.visible_column(static_cast<std::size_t>(ordinal));
        const std::uint32_t want = kResourceEntryColumns[ordinal + 1].field;
        check(column != core::sheet::kNoIndex &&
                  stored.column(column).field == want,
              "resource gate: the live column follows COM's TableFields order");
        check(model->headerData(ordinal, Qt::Horizontal, Qt::DisplayRole)
                      .toString() == QString::fromUtf8(titles[ordinal]),
              "resource gate: the live caption matches the stored table/field caption");
        // Project's width: the COM character count at this view's measured
        // 6 px/char (Segoe UI 10) plus the 3 px border.
        const int want_width = msqt::column_width_px(
            kResourceEntryColumns[ordinal + 1].chars, msqt::kColumnCharPx);
        if (sheet->columnWidth(ordinal) != want_width) {
            std::fprintf(stderr, "FAIL: live column %d width %d, want %d\n", ordinal,
                         sheet->columnWidth(ordinal), want_width);
            ++g_failures;
        }
    }
    check(model->indicators_column() == 0,
          "resource gate: the live Indicators column is the first scrolling column");
    check(sheet->verticalHeader() != nullptr &&
              sheet->verticalHeader()->defaultSectionSize() == msqt::kRowHeight,
          "resource gate: the live Resource Sheet keeps the 22 px row pitch");
    check(sheet->verticalHeader() != nullptr &&
              sheet->verticalHeader()->width() + msqt::kViewLabelWidth ==
                  msqt::kGutterWidth,
          "resource gate: the live Resource Sheet keeps the 60 px frozen gutter");
    check(sheet->horizontalHeader() != nullptr &&
              sheet->horizontalHeader()->height() == msqt::kSheetHeaderHeight,
          "resource gate: the live Resource Sheet keeps the 42 px caption band");

    // COM's stored Resource.ID values, drawn in the frozen gutter.
    check(model->rowCount() == 3, "resource gate: the live sheet shows three resources");
    if (model->rowCount() != 3) {
        return;
    }
    check(model->row_number_text(0) == QStringLiteral("1") &&
              model->row_number_text(1) == QStringLiteral("2") &&
              model->row_number_text(2) == QStringLiteral("3"),
          "resource gate: the live gutter reads COM's Resource.ID 1/2/3");

    // COM's resource values through the live cells (W31-G3-probe.json).  The
    // columns are the visible ordinals: 1 Name, 2 Type, 4 Initials, 5 Group,
    // 6 Max. Units, 7 Std. Rate, 8 Ovt. Rate, 9 Cost/Use, 10 Accrue At,
    // 11 Base Calendar, 12 Code.
    const auto cell = [&model](int row, int column) {
        return model->index(row, column).data(Qt::DisplayRole).toString();
    };
    check(cell(0, 1) == QStringLiteral("Crew A") &&
              cell(1, 1) == QStringLiteral("Crew B") &&
              cell(2, 1) == QStringLiteral("Crew C"),
          "resource gate: the live Name cells are COM's resource names");
    check(cell(0, 2) == QStringLiteral("Work") &&
              cell(1, 2) == QStringLiteral("Work") &&
              cell(2, 2) == QStringLiteral("Work"),
          "resource gate: the live Type cells are COM's work resources");
    check(cell(0, 3).isEmpty() && cell(1, 3).isEmpty() && cell(2, 3).isEmpty(),
          "resource gate: the live Material Label cells are COM's empty labels");
    check(cell(0, 4) == QStringLiteral("C") && cell(1, 4) == QStringLiteral("C") &&
              cell(2, 4) == QStringLiteral("C"),
          "resource gate: the live Initials cells are COM's C");
    check(cell(0, 5) == QStringLiteral("North") &&
              cell(1, 5) == QStringLiteral("South") &&
              cell(2, 5) == QStringLiteral("North"),
          "resource gate: the live Group cells are COM's North/South/North");
    check(cell(0, 6) == QStringLiteral("100%") &&
              cell(1, 6) == QStringLiteral("100%") &&
              cell(2, 6) == QStringLiteral("100%"),
          "resource gate: the live Max. Units cells are COM's 100 %");
    check(cell(0, 7) == QStringLiteral("$0.00/hr") &&
              cell(1, 7) == QStringLiteral("$50.00/hr") &&
              cell(2, 7) == QStringLiteral("$0.00/hr"),
          "resource gate: the live Std. Rate cells are COM's 0/50/0 per hour");
    check(cell(0, 8) == QStringLiteral("$0.00/hr") &&
              cell(1, 8) == QStringLiteral("$75.00/hr") &&
              cell(2, 8) == QStringLiteral("$0.00/hr"),
          "resource gate: the live Ovt. Rate cells are COM's 0/75/0 per hour");
    check(cell(0, 9) == QStringLiteral("$0.00") &&
              cell(1, 9) == QStringLiteral("$10.00") &&
              cell(2, 9) == QStringLiteral("$0.00"),
          "resource gate: the live Cost/Use cells are COM's 0/10/0");
    check(cell(0, 10) == QStringLiteral("Prorated") &&
              cell(1, 10) == QStringLiteral("Start") &&
              cell(2, 10) == QStringLiteral("End"),
          "resource gate: the live Accrue At cells are COM's Prorated/Start/End");
    check(cell(0, 11) == QStringLiteral("Standard") &&
              cell(1, 11) == QStringLiteral("Standard") &&
              cell(2, 11) == QStringLiteral("Standard"),
          "resource gate: the live Base Calendar cells are COM's Standard");
    check(cell(0, 12) == QStringLiteral("CR-A") &&
              cell(1, 12) == QStringLiteral("CR-B") &&
              cell(2, 12) == QStringLiteral("CR-C"),
          "resource gate: the live Code cells are COM's CR-A/CR-B/CR-C");
    check(cell(0, 0).isEmpty() && cell(1, 0).isEmpty() && cell(2, 0).isEmpty(),
          "resource gate: the live Indicators cells stay captionless blanks");

    // COM's Overallocated: only Crew A, painted with the view's slot 3.
    const QFont overallocated_font = model->index(0, 1).data(Qt::FontRole).value<QFont>();
    const QColor overallocated_colour =
        model->index(0, 1).data(Qt::ForegroundRole).value<QColor>();
    check(overallocated_font.bold() && overallocated_colour.isValid() &&
              overallocated_colour.red() == 165 && overallocated_colour.green() == 0 &&
              overallocated_colour.blue() == 0,
          "resource gate: the live overallocated row is slot 3's bold RGB 165,0,0");
    const QFont normal_font = model->index(1, 1).data(Qt::FontRole).value<QFont>();
    check(!normal_font.bold() &&
              !model->index(1, 1).data(Qt::ForegroundRole).isValid(),
          "resource gate: the live ordinary row is slot 2's upright automatic text");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: qt_resource_sheet_gate_tests "
                     "<wave31-resource-sheet.mpp>\n");
        return 2;
    }
    const std::string fixture = argv[1];
    QApplication application(argc, argv);
    msqt::PlanDocument document;
    std::string error;
    if (!document.load(fixture, app::ZoomLevel::week, error)) {
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
    check_resource_model(document);
    check_qt_model(document);
    check_live_window(fixture);

    std::printf("qt_resource_sheet_gate: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
