// app_qt/tests/clipboard_test.cpp -- wave 20's clipboard gate.
//
// The shell's Copy/Cut/Paste are core::sheet::SheetModel operations on the
// selected rectangle (values only: the typed Value and its rendering, never a
// font or a colour), so this test drives the REAL menu actions against the real
// fixture and asserts:
//
//   * Copy is disabled with no document and enabled with a selection;
//   * Paste is disabled until something has been copied, then enabled;
//   * a copied range pastes across a range and becomes one undo step;
//   * Undo restores the pre-paste cells;
//   * Cut copies and clears in ONE undoable transaction, and Undo restores it;
//   * the shell's own can_paste()/can_copy() rules drive the menu enablement.
//
// Nothing is written under tests/data; the fixture is opened read-only.
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QString>
#include <QTableView>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "entry_columns.h"
#include "main_window.h"
#include "menu_model.h"
#include "sheet.h"

#include "gate_util.h"

namespace {

std::string cell_text(msqt::MainWindow& window, std::size_t row, std::size_t column) {
    return window.sheet() != nullptr ? window.sheet()->cell_text(row, column) : std::string();
}

bool action_enabled(msqt::MainWindow& window, app::MenuAction id) {
    QAction* action = gate::find_action(window, id);
    return action != nullptr && action->isEnabled();
}

void trigger(msqt::MainWindow& window, app::MenuAction id) {
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, "the menu carries the clipboard action");
    if (action != nullptr) {
        action->trigger();
        QApplication::processEvents();
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_clipboard_tests <fixture.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);

    // --- no document: everything disabled --------------------------------
    {
        msqt::MainWindow empty;
        empty.show();
        QApplication::processEvents();
        gate::check(!action_enabled(empty, app::MenuAction::copy),
                    "Copy is disabled with no document");
        gate::check(!action_enabled(empty, app::MenuAction::cut),
                    "Cut is disabled with no document");
        gate::check(!action_enabled(empty, app::MenuAction::paste),
                    "Paste is disabled with no document");
    }

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();

    core::sheet::SheetModel* sheet = window.sheet();
    gate::check(sheet != nullptr, "the window exposes the core sheet model");
    if (sheet == nullptr) {
        std::printf("qt_clipboard: ");
        return gate::finish();
    }
    gate::check(sheet->row_count() >= 3, "the fixture has at least three rows");

    const std::size_t name_column = window.sheet_table_model()->visible_column_for_field(
        msqt::entry_field(msqt::EntryCellKind::name));
    const std::size_t duration_column = window.sheet_table_model()->visible_column_for_field(
        msqt::entry_field(msqt::EntryCellKind::duration));
    gate::check(name_column != core::sheet::kNoIndex, "the sheet has a Task Name column");
    gate::check(duration_column != core::sheet::kNoIndex, "the sheet has a Duration column");

    // --- Copy needs a selection; Paste needs a block -----------------------
    gate::check(sheet->select_range(0, 1, name_column, name_column),
                "a two-row Name selection is made");
    window.refresh_menu_state();
    gate::check(action_enabled(window, app::MenuAction::copy),
                "Copy is enabled with a selection");
    gate::check(action_enabled(window, app::MenuAction::cut), "Cut is enabled with a selection");
    gate::check(!action_enabled(window, app::MenuAction::paste),
                "Paste is disabled before anything is copied");

    const std::string first_name = cell_text(window, 0, name_column);
    const std::string second_name = cell_text(window, 1, name_column);
    gate::check(!first_name.empty() && !second_name.empty(), "the fixture's names are non-empty");
    trigger(window, app::MenuAction::copy);
    gate::check(window.clipboard_block().rows == 2 && window.clipboard_block().columns == 1,
                "Copy captured the 2x1 selection as a values block");
    gate::check(action_enabled(window, app::MenuAction::paste),
                "Paste is enabled once the block exists");

    // --- paste the copied 2x1 range at row 1: one undo step -----------------
    const std::string row1_before = cell_text(window, 1, name_column);
    const std::string row2_before = cell_text(window, 2, name_column);
    gate::check(sheet->select_cell(1, name_column), "the paste target cell is selected");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::paste);
    gate::check(cell_text(window, 1, name_column) == first_name,
                "paste wrote the block's first cell at the target");
    gate::check(cell_text(window, 2, name_column) == second_name,
                "paste walked visible rows for the second cell");
    gate::check(action_enabled(window, app::MenuAction::undo), "Paste created an undo step");

    trigger(window, app::MenuAction::undo);
    gate::check(cell_text(window, 1, name_column) == row1_before,
                "Undo restores the first pasted row");
    gate::check(cell_text(window, 2, name_column) == row2_before,
                "Undo restores the second pasted row");

    // --- a 2x2 range copy/paste (values only) ------------------------------
    gate::check(sheet->select_range(0, 1, name_column, duration_column),
                "a 2x2 Name+Duration selection is made");
    const std::string name1 = cell_text(window, 1, name_column);
    const std::string duration1 = cell_text(window, 1, duration_column);
    const std::string name2 = cell_text(window, 2, name_column);
    const std::string duration2 = cell_text(window, 2, duration_column);
    window.refresh_menu_state();
    trigger(window, app::MenuAction::copy);
    gate::check(window.clipboard_block().rows == 2 && window.clipboard_block().columns == 2,
                "Copy captured the 2x2 range");
    const std::string source_name = cell_text(window, 0, name_column);
    const std::string source_duration = cell_text(window, 0, duration_column);
    // Paste at row 1, Name: it writes rows 1 and 2 (row 0 was the source).
    gate::check(sheet->select_cell(1, name_column), "the 2x2 paste target is selected");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::paste);
    gate::check(cell_text(window, 1, name_column) == source_name,
                "the 2x2 paste wrote Name at the target row");
    gate::check(cell_text(window, 1, duration_column) == source_duration,
                "the 2x2 paste wrote Duration at the target row");
    trigger(window, app::MenuAction::undo);
    gate::check(cell_text(window, 1, name_column) == name1 &&
                    cell_text(window, 1, duration_column) == duration1 &&
                    cell_text(window, 2, name_column) == name2 &&
                    cell_text(window, 2, duration_column) == duration2,
                "one Undo restores the whole 2x2 paste");

    // --- Cut: copy + one clearing transaction -------------------------------
    const std::string cut_row0 = cell_text(window, 0, name_column);
    const std::string cut_row1 = cell_text(window, 1, name_column);
    gate::check(sheet->select_range(0, 1, name_column, name_column),
                "a two-row Name selection is made for Cut");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::cut);
    gate::check(window.clipboard_block().rows == 2,
                "Cut captured the selection before clearing it");
    gate::check(cell_text(window, 0, name_column).empty() &&
                    cell_text(window, 1, name_column).empty(),
                "Cut cleared the selected cells");
    gate::check(action_enabled(window, app::MenuAction::undo), "Cut created an undo step");
    trigger(window, app::MenuAction::undo);
    gate::check(cell_text(window, 0, name_column) == cut_row0 &&
                    cell_text(window, 1, name_column) == cut_row1,
                "one Undo restores the whole Cut");

    // --- a single cell is the minimum supported selection --------------------
    gate::check(sheet->select_cell(0, name_column), "a single cell can be selected");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::copy);
    gate::check(window.clipboard_block().rows == 1 && window.clipboard_block().columns == 1,
                "Copy supports a single selected cell");
    gate::check(sheet->select_cell(2, name_column), "a single paste target is selected");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::paste);
    gate::check(cell_text(window, 2, name_column) == first_name,
                "single-cell paste writes the one copied value");
    trigger(window, app::MenuAction::undo);
    gate::check(cell_text(window, 2, name_column) == row2_before,
                "single-cell paste is undone in one step");

    std::printf("qt_clipboard: ");
    return gate::finish();
}
