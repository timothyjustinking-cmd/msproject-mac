// app_qt/entry_columns.cpp -- see entry_columns.h.

#include "entry_columns.h"

#include <cstddef>

#include "layout_model.h"  // app::task_field_id

namespace msqt {
namespace {

core::view::FieldId model_field(const char* name) { return app::task_field_id(name); }

// The stock Entry table's stored character widths: Indicators 6, Task Mode 9,
// Task Name 24, Duration/Start/Finish 13, Predecessors/Resources 17, and the
// 20-character "Add New Column" stub.  entry_width_px() turns them into the
// measured 39/57/147/81/81/81/105/105/123.  At load, a width actually stored in
// the file's `214/CTable` replaces default_chars (MainWindow::configure_sheet).
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
        {"Predecessors", 0u, 17, entry_width_px(17), EntryCellKind::predecessors,
         0x0b40002fu},
        {"Resource Names", 0u, 17, entry_width_px(17), EntryCellKind::resources, 0x0b400031u},
        {"Add New Column", 0u, 20, entry_width_px(20), EntryCellKind::stub, 0u},
    };
    return kColumns;
}

}  // namespace

const std::vector<EntryColumn>& entry_columns() { return columns(); }

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
