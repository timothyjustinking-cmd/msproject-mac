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
#include "shell_style.h"

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
    std::fprintf(stderr, "note: visible model columns:");
    for (std::size_t column : sheet->visible_columns()) {
        std::fprintf(stderr, " %zu:%s", column, sheet->column_title(column).c_str());
    }
    std::fprintf(stderr, "\n");

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
    // The stored table's model order is UID, ID, Outline, Name, Start, Finish,
    // Duration while the Entry display order is Name, Duration, Start, Finish.
    // A core selection rectangle is expressed in MODEL columns, so a display
    // Name-to-Duration drag covers four model columns.  The 2x2 gate therefore
    // uses Start+Finish, which are adjacent in both orders; the display-order
    // limitation is recorded in docs/handoff/qt2.md.
    const std::size_t start_column =
        window.sheet_table_model()->visible_column_for_field(
            msqt::entry_field(msqt::EntryCellKind::start));
    const std::size_t finish_column =
        window.sheet_table_model()->visible_column_for_field(
            msqt::entry_field(msqt::EntryCellKind::finish));
    gate::check(start_column != core::sheet::kNoIndex && finish_column == start_column + 1,
                "Start and Finish are adjacent model columns");
    gate::check(sheet->select_range(0, 1, start_column, finish_column),
                "a 2x2 Start+Finish selection is made");
    const std::string start1 = cell_text(window, 1, start_column);
    const std::string finish1 = cell_text(window, 1, finish_column);
    const std::string start2 = cell_text(window, 2, start_column);
    const std::string finish2 = cell_text(window, 2, finish_column);
    window.refresh_menu_state();
    trigger(window, app::MenuAction::copy);
    std::fprintf(stderr, "note: 2x2 copy over model columns start=%zu finish=%zu -> %zux%zu\n",
                 start_column, finish_column, window.clipboard_block().rows,
                 window.clipboard_block().columns);
    gate::check(window.clipboard_block().rows == 2 && window.clipboard_block().columns == 2,
                "Copy captured the 2x2 range");
    const std::string source_start = cell_text(window, 0, start_column);
    const std::string source_finish = cell_text(window, 0, finish_column);
    gate::check(!source_start.empty() && !source_finish.empty(),
                "the 2x2 source cells carry values");
    // W32-0.2 (R31-2, High): a paste block covering Start is refused WHOLE
    // before any write (no partial write of the Finish column either), with the
    // decision-9 reason in the status line.
    gate::check(sheet->select_cell(1, start_column), "the 2x2 paste target is selected");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::paste);
    gate::check(cell_text(window, 1, start_column) == start1,
                "the refused 2x2 paste leaves the target Start unchanged");
    gate::check(cell_text(window, 1, finish_column) == finish1,
                "the refused 2x2 paste leaves the target Finish unchanged (no partial write)");
    gate::check(cell_text(window, 2, start_column) == start2 &&
                    cell_text(window, 2, finish_column) == finish2,
                "the refused 2x2 paste leaves the second target row unchanged");
    const QString refusal = window.status_left_text();
    std::fprintf(stderr, "note: 2x2 paste refusal: %s\n", refusal.toLocal8Bit().constData());
    gate::check(refusal.contains(QStringLiteral("Start is read-only")),
                "the 2x2 refusal names Start read-only");
    gate::check(refusal.contains(QStringLiteral("not measured against Project")),
                "the 2x2 refusal gives the decision-9 reason");

    // --- W32-0.2: Cut refuses a selection that touches Start ----------------
    const std::string cut_start_before = cell_text(window, 0, start_column);
    gate::check(sheet->select_cell(0, start_column), "a Start cell is selected for Cut");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::cut);
    gate::check(cell_text(window, 0, start_column) == cut_start_before,
                "the refused Cut leaves the Start cell unchanged");
    const QString cut_refusal = window.status_left_text();
    std::fprintf(stderr, "note: Cut Start refusal: %s\n",
                 cut_refusal.toLocal8Bit().constData());
    gate::check(cut_refusal.contains(QStringLiteral("Start is read-only")),
                "the Cut refusal names Start read-only");
    gate::check(cut_refusal.contains(QStringLiteral("not measured against Project")),
                "the Cut refusal gives the decision-9 reason");

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
