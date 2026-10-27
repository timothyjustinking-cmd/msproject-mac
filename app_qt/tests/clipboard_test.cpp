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
#include "filter.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "sheet.h"
#include "shell_style.h"
#include "sort.h"

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

// W32-0.3: QAction::trigger() does nothing on a disabled action, so a gate that
// only calls trigger() proves the enablement rule and not the handler's own
// refusal.  Force-enabling first exercises the handler (a ribbon button or any
// direct caller bypasses the menu); refresh_menu_state() restores the real
// enablement afterwards.
void force_trigger(msqt::MainWindow& window, app::MenuAction id) {
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, "the menu carries the clipboard action");
    if (action != nullptr) {
        action->setEnabled(true);
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

    // --- W32-0.3 (R31-3, High): a reordered display refuses range operations --
    // Core selection coordinates and SheetModel::paste walk FILE rows.  The
    // block below is three rows of Duration text, so any write into the Name
    // column would be visible; it is copied BEFORE the sort/filter because a
    // range copy is itself refused once the display is reordered.
    const int name_display_column =
        window.sheet_table_model()->display_column_for_field(app::name_field_id());
    gate::check(name_display_column >= 0, "the display has a Name column");
    gate::check(sheet->select_range(0, 2, duration_column, duration_column),
                "a three-row Duration block is selected");
    window.refresh_menu_state();
    trigger(window, app::MenuAction::copy);
    gate::check(window.clipboard_block().rows == 3,
                "the three-row Duration block is on the clipboard");
    const std::string block3_text = window.clipboard_block().to_text();
    gate::check(!block3_text.empty(), "the three-row block carries text");

    // `label` names the state in every message, so the sort and filter runs
    // cannot be confused in a failure log.
    const auto check_reordered = [&](const std::string& label) {
        auto check = [&label](bool condition, const std::string& what) {
            gate::check(condition, (label + ": " + what).c_str());
        };
        core::sheet::SheetModel* core = window.sheet();
        check(core != nullptr, "the window exposes the sheet");
        if (core == nullptr || name_display_column < 0) {
            return;
        }
        check(window.display_order_differs_from_file(),
              "the display order is not the file order");
        check(window.sheet_table_model()->record_row(0) != core::sheet::kNoIndex,
              "display row 0 maps to a record");

        std::vector<std::string> names;
        for (std::size_t row = 0; row < core->row_count(); ++row) {
            names.push_back(cell_text(window, row, name_column));
        }

        // Range Copy and Cut are disabled, and their handlers refuse too.
        check(core->select_range(0, 2, name_column, name_column),
              "a three-row Name range is selected");
        window.refresh_menu_state();
        check(!action_enabled(window, app::MenuAction::copy),
              "Copy of a range is disabled");
        check(!action_enabled(window, app::MenuAction::cut),
              "Cut of a range is disabled");
        force_trigger(window, app::MenuAction::copy);
        check(window.clipboard_block().to_text() == block3_text,
              "the refused Copy leaves the clipboard block unchanged");
        check(window.status_left_text().contains(QStringLiteral("copy: a range is refused")),
              "the Copy handler names the range refusal");
        window.refresh_menu_state();
        force_trigger(window, app::MenuAction::cut);
        check(window.clipboard_block().to_text() == block3_text,
              "the refused Cut leaves the clipboard block unchanged");
        bool names_intact = true;
        for (std::size_t row = 0; row < core->row_count(); ++row) {
            names_intact = names_intact && cell_text(window, row, name_column) == names[row];
        }
        check(names_intact, "the refused Cut leaves every Name cell unchanged");
        check(window.status_left_text().contains(QStringLiteral("cut: a range is refused")),
              "the Cut handler names the range refusal");
        window.refresh_menu_state();

        // The paste target must be a REAL task row: the project-summary row is
        // refused by a different guard (wave 25's uid-0 rule), so a paste there
        // would pass "nothing changed" without exercising this refusal.
        int task_display_row = -1;
        for (int display = 0; display < window.sheet_table_model()->rowCount(); ++display) {
            const std::size_t record = window.sheet_table_model()->record_row(display);
            if (record != core::sheet::kNoIndex && core->row_uid(record) != 0) {
                task_display_row = display;
                break;
            }
        }
        check(task_display_row >= 0, "the display shows a real task row");
        if (task_display_row < 0) {
            return;
        }

        // A three-row Paste is disabled and the handler writes nothing.
        window.clicked_cell(task_display_row, name_display_column, false);
        window.refresh_menu_state();
        check(!action_enabled(window, app::MenuAction::paste),
              "a three-row Paste is disabled");
        force_trigger(window, app::MenuAction::paste);
        names_intact = true;
        for (std::size_t row = 0; row < core->row_count(); ++row) {
            names_intact = names_intact && cell_text(window, row, name_column) == names[row];
        }
        check(names_intact, "the refused Paste leaves every Name cell unchanged");
        check(window.status_left_text().contains(QStringLiteral("multi-row block is refused")),
              "the Paste handler names the multi-row refusal");
        window.refresh_menu_state();

        // A single-cell edit on a clicked display row changes exactly the
        // record shown there (the adapter maps display row -> file row).  The
        // project-summary row is skipped: a cell edit there is refused for a
        // different reason (wave 25's uid-0 rule).
        const std::size_t shown = window.sheet_table_model()->record_row(task_display_row);
        check(window.sheet_table_model()
                      ->data(window.sheet_table_model()->index(task_display_row,
                                                               name_display_column),
                             Qt::DisplayRole)
                      .toString()
                      .toStdString() == names[shown],
              "the task display row shows the mapped record's Name");
        const QString edited = QStringLiteral("edited on a display row");
        check(window.sheet_table_model()->setData(
                  window.sheet_table_model()->index(task_display_row, name_display_column),
                  edited, Qt::EditRole),
              "a single-cell edit on the task display row commits");
        check(core->field_text(shown, app::name_field_id()) == edited.toStdString(),
              "the edit changed exactly the record shown there");
        bool others_intact = true;
        for (std::size_t row = 0; row < core->row_count(); ++row) {
            if (row == shown) {
                continue;
            }
            others_intact =
                others_intact && cell_text(window, row, name_column) == names[row];
        }
        check(others_intact, "no other record's Name changed");
        check(window.sheet_table_model()->setData(
                  window.sheet_table_model()->index(task_display_row, name_display_column),
                  QString::fromStdString(names[shown]), Qt::EditRole),
              "the display-row edit is restored");

        // Shift-click does not extend the selection.
        const int other_display_row = task_display_row == 0 ? 1 : 0;
        window.clicked_cell(task_display_row, name_display_column, false);
        check(core->selected_cells().size() == 1, "a plain click selects one cell");
        const std::size_t anchor_row = core->anchor().row;
        window.clicked_cell(other_display_row, name_display_column, true);
        check(core->selected_cells().size() == 1, "a Shift-click does not extend");
        check(core->anchor().row == anchor_row, "the Shift-click leaves the anchor");
        check(window.status_left_text().contains(QStringLiteral("shift-click")),
              "the refused extension explains itself");
    };

    // W32-0.3: an ID-DESCENDING sort reorders the display even though it names
    // the ID field, so every range operation is refused until the sort clears.
    core::view::SortDefinition sort;
    sort.id = "w32-id-desc";
    sort.name = "ID descending";
    sort.keys.push_back(core::view::SortKey{app::task_field_id("ID"), false});
    window.apply_session_sort(std::move(sort), "ID descending");
    QApplication::processEvents();
    gate::check(window.sort_active(), "the ID-descending sort is active");
    check_reordered("ID-descending sort");
    // R31-4 folded into W32-0.3: the file-order commands refuse while a sort
    // reorders the rows.  The selection is a real task cell, so without the
    // sort rule Indent and Outdent would be enabled.
    gate::check(!action_enabled(window, app::MenuAction::insert_task),
                "ID-descending sort: Insert is disabled");
    gate::check(!action_enabled(window, app::MenuAction::indent),
                "ID-descending sort: Indent is disabled");
    gate::check(!action_enabled(window, app::MenuAction::outdent),
                "ID-descending sort: Outdent is disabled");
    window.clear_session_sort();
    QApplication::processEvents();
    gate::check(!window.sort_active(), "the sort is cleared");
    gate::check(action_enabled(window, app::MenuAction::insert_task),
                "clearing the sort enables Insert again");

    // An active filter is refused even when it keeps every row and the file's
    // order: the shell cannot know a display list is the file's.
    core::view::FilterDefinition filter;
    filter.id = "w32-named";
    filter.name = "named tasks";
    filter.criteria = core::view::FilterCriterion::is_not_null(app::name_field_id());
    window.apply_session_filter(std::move(filter), "named tasks");
    QApplication::processEvents();
    gate::check(window.filter_active(), "the filter is active");
    check_reordered("filter");
    window.clear_session_filter();
    QApplication::processEvents();
    gate::check(!window.filter_active(), "the filter is cleared");

    std::printf("qt_clipboard: ");
    return gate::finish();
}
