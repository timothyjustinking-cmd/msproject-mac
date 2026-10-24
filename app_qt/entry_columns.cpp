// app_qt/entry_columns.cpp -- see entry_columns.h.

#include "entry_columns.h"

#include <QFont>
#include <QFontMetrics>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "layout_model.h"  // app::task_field_id

namespace msqt {
namespace {

core::view::FieldId model_field(const char* name) { return app::task_field_id(name); }

// The stock Entry table's stored character widths: Indicators 6, Task Mode 9,
// Task Name 24, Duration/Start/Finish 13, Predecessors/Resources 17, and the
// 20-character "Add New Column" stub.  entry_width_px() turns them into the
// measured 39/57/147/81/81/81/105/105/123 (the fixture's Segoe UI 10 unit).  At
// load, a width actually stored in the file's `214/CTable` replaces
// default_chars and the FILE's measured unit converts it
// (display_columns_for_sheet).
const std::vector<EntryColumn>& columns() {
    static const std::vector<EntryColumn> kColumns = {
        {"", 0u, 6, entry_width_px(6), EntryCellKind::indicators, 0x0b4000cdu},
        {"Task Mode", 0u, 9, entry_width_px(9), EntryCellKind::mode, 0x0b400500u},
        {"Task Name", model_field("Name"), 24, entry_width_px(24), EntryCellKind::name,
         0x0b40000eu},
        {"Duration", model_field("Duration"), 13, entry_width_px(13), EntryCellKind::duration,
         0x0b400507u},
        {"Start", model_field("Start"), 13, entry_width_px(13), EntryCellKind::start,
         0x0b400505u},
        {"Finish", model_field("Finish"), 13, entry_width_px(13), EntryCellKind::finish,
         0x0b400506u},
        {"Predecessors", kPredecessorsField, 17, entry_width_px(17),
         EntryCellKind::predecessors, 0x0b40002fu},
        {"Resource Names", kResourceNamesField, 17, entry_width_px(17),
         EntryCellKind::resources, 0x0b400031u},
        {"Add New Column", 0u, 20, entry_width_px(20), EntryCellKind::stub, 0u},
    };
    return kColumns;
}

// The stock width for a field whose file column stores none: the Entry column's
// own character width when this field has one (matched by model field or by the
// CTable field id), else Project's measured fresh-column header (12 characters),
// converted at the file's own unit.
int stock_display_width(core::view::FieldId field, int char_px) {
    for (const EntryColumn& column : columns()) {
        if (field != 0 && (column.field == field || column.table_field == field)) {
            return column_width_px(column.default_chars, char_px);
        }
    }
    return column_width_px(12, char_px);
}

// MEASURED on this box (build/w29/font_probe2, Qt 6.5.3/GDI): the lowercase
// alphabet advances sum to 133 px at Arial 8, 167 px at Segoe UI 10 and 179 px
// at Calibri 11 and Carlito 11.  133/26 = 5.12 -> 5, 167/26 = 6.42 -> 6,
// 179/26 = 6.88 -> 7.  The table keeps those answers independent of the faces a
// CI runner happens to have installed (the corpus and fixture fonts must paint
// the same width on Linux and macOS as on Windows).
struct MeasuredCharPx {
    const char* family;
    int point_size;
    int px;
};

constexpr MeasuredCharPx kMeasuredCharPx[] = {
    {"Arial", 8, 5},
    {"Segoe UI", 10, 6},
    {"Calibri", 11, 7},
    {"Carlito", 11, 7},
};

}  // namespace

int column_char_px(const std::string& family, int point_size) {
    if (family.empty() || point_size <= 0) {
        return kColumnCharPx;
    }
    const QString wanted = QString::fromStdString(family);
    for (const MeasuredCharPx& measured : kMeasuredCharPx) {
        if (point_size == measured.point_size &&
            wanted.compare(QString::fromLatin1(measured.family), Qt::CaseInsensitive) == 0) {
            return measured.px;
        }
    }
    // Unmeasured face: the same rule, measured live.  Round half up; a font
    // engine that reports nothing usable keeps the stock unit.
    QFont font(wanted);
    font.setPointSize(point_size);
    const QFontMetrics metrics(font);
    int sum = 0;
    for (char letter = 'a'; letter <= 'z'; ++letter) {
        sum += metrics.horizontalAdvance(QLatin1Char(letter));
    }
    if (sum <= 0) {
        return kColumnCharPx;
    }
    return std::clamp((sum + 13) / 26, 1, 64);
}

const std::vector<EntryColumn>& entry_columns() { return columns(); }

const std::vector<DisplayColumn>& entry_display_columns() {
    static const std::vector<DisplayColumn> kDisplay = []() {
        std::vector<DisplayColumn> display;
        display.reserve(columns().size());
        for (const EntryColumn& column : columns()) {
            DisplayColumn shown;
            shown.title = column.title;
            shown.field = column.field;
            // W29-1.2: the Entry list's stored ids are the same `214/CTable`
            // ids the applied view's per-cell formatting record keys on.
            shown.table_field = column.table_field;
            shown.width = column.width;
            shown.kind = column.kind;
            display.push_back(std::move(shown));
        }
        return display;
    }();
    return kDisplay;
}

EntryCellKind display_kind_for_field(core::view::FieldId field) {
    if (field == 0) {
        return EntryCellKind::data;  // the stub sets its own kind
    }
    if (field == entry_field(EntryCellKind::name)) {
        return EntryCellKind::name;
    }
    if (field == entry_field(EntryCellKind::duration)) {
        return EntryCellKind::duration;
    }
    if (field == entry_field(EntryCellKind::start)) {
        return EntryCellKind::start;
    }
    if (field == entry_field(EntryCellKind::finish)) {
        return EntryCellKind::finish;
    }
    switch (field) {
        case 0x0b4000cdu: return EntryCellKind::indicators;   // Indicators (no model field)
        case 0x0b400500u: return EntryCellKind::mode;         // Task.Manual = Task Mode
        case kPredecessorsField: return EntryCellKind::predecessors;
        case kSuccessorsField: return EntryCellKind::successors;
        case kResourceNamesField: return EntryCellKind::resources;
        default: break;
    }
    return EntryCellKind::data;
}

std::vector<DisplayColumn> display_columns_for_sheet(const core::sheet::SheetModel& sheet,
                                                     int char_px) {
    std::vector<DisplayColumn> display;
    const core::view::FieldId id_field = app::task_field_id("ID");
    display.reserve(sheet.visible_column_count() + 1);
    for (std::size_t ordinal = 0; ordinal < sheet.visible_column_count(); ++ordinal) {
        const std::size_t column = sheet.visible_column(ordinal);
        if (column == core::sheet::kNoIndex) {
            continue;
        }
        const core::sheet::SheetColumn& source = sheet.column(column);
        if (id_field != 0 && source.field == id_field) {
            // Project draws the ID column as the frozen row-number gutter, not as
            // a scrolling column (SheetRowHeader), so it is not a display column.
            continue;
        }
        DisplayColumn shown;
        shown.title = source.title;
        shown.field = source.field;
        // W29-1.2: carry the stored `214/CTable` id through for the cell-format
        // lookup (the model field above is the mapped one, e.g. Duration rather
        // than DurationText).  A hand-built column with no file record behind it
        // falls back to its model field, which is what the stock Entry list uses.
        shown.table_field = source.table_field != 0 ? source.table_field : source.field;
        shown.kind = display_kind_for_field(source.field);
        // W29-1.1: the stored width is characters; the file's own font sets
        // the pixel unit.  A stored 0 never gets here -- table_sheet_columns()
        // marks such a column hidden (Project draws none for PROJECT_A's
        // Indicators) -- but a visible column with no width keeps the fallback.
        shown.width = source.width > 0 ? column_width_px(source.width, char_px)
                                       : stock_display_width(source.field, char_px);
        display.push_back(std::move(shown));
    }
    if (display.empty()) {
        // Defensive: no usable sheet column means the Entry presentation is the
        // only honest table to show (an empty caption band would lose the product's
        // structure), and it is what a new/empty project shows anyway.
        return entry_display_columns();
    }
    // Project's sheet always ends with the Add New Column affordance; it is not a
    // stored column and the reference measures its 20-character width.
    DisplayColumn stub;
    stub.title = "Add New Column";
    stub.field = 0;
    stub.width = column_width_px(20, char_px);
    stub.kind = EntryCellKind::stub;
    display.push_back(std::move(stub));
    return display;
}

int display_content_width(const std::vector<DisplayColumn>& columns) {
    int total = 0;
    for (const DisplayColumn& column : columns) {
        total += column.width;
    }
    return total;
}

int entry_column_index(EntryCellKind kind) {
    const std::vector<EntryColumn>& all = columns();
    for (std::size_t index = 0; index < all.size(); ++index) {
        if (all[index].kind == kind) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int entry_content_width() {
    int total = 0;
    for (const EntryColumn& column : columns()) {
        total += column.width;
    }
    return total;
}

core::view::FieldId entry_field(EntryCellKind kind) {
    for (const EntryColumn& column : columns()) {
        if (column.kind == kind) {
            return column.field;
        }
    }
    return 0;
}

}  // namespace msqt
