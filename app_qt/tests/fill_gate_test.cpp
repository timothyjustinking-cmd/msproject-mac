// app_qt/tests/fill_gate_test.cpp -- wave 30 (W30-1.8).
//
// THE TARGET (wave 30 brief, G1): Fill Down fills the SELECTED cells, but only
// for a column whose write is already measured.  This port's one measured cell
// writer is Start (app::collect_start_edits -> the wave-19 start/SNET writer),
// so Fill Down copies the top visible Start cell down a multi-row Start
// selection and refuses every other shape before it stages anything.
//
// WHAT THIS GATE DOES (all through live widgets and the real menu actions)
//   1. opens tests/data/wave23-outline.mpp and finds the Task tab / Editing
//      group's Fill button: it is enabled (W30-1.8) and carries
//      RibbonCommand::fill_down;
//   2. refusals, each proven to leave the document untouched:
//        - a two-row Task Name selection: no measured writer for Name;
//        - a one-row Start selection: nothing below the source;
//        - the project summary row as the source (the row is read-only);
//        - a Start selection whose top cell was cleared by Cut;
//   3. the fill: uid 4 starts 2026-12-16 17:00, uids 5 and 6 start 2026-12-11,
//      so a uid 4..6 Start selection filled down writes exactly those two
//      cells, produces exactly two pending start edits and is dirty;
//   4. ONE Undo restores both starts and the document is clean again;
//   5. the row menu's Fill Down runs the same path and is one undo step;
//   6. in a second window a pending Insert Task row is the last selected row:
//      Fill Down refuses the whole selection because the save diff skips a
//      pending insert, so filling it would be silently dropped.
//
// Usage: qt_fill_gate <fixture.mpp> [out-dir]
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QInputDialog>
#include <QMenu>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "entry_columns.h"
#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "sheet.h"
#include "shell_style.h"

namespace {

QAbstractButton* fill_button(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int task_tab = ribbon->tab_titles().indexOf(QStringLiteral("Task"));
    if (task_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(task_tab);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int editing = groups.indexOf(QStringLiteral("Editing"));
    if (editing < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(editing); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(editing, control));
        if (button != nullptr && button->text() == QStringLiteral("Fill")) {
            return button;
        }
    }
    return nullptr;
}

std::size_t start_column(msqt::MainWindow& window) {
    auto* model = window.sheet_table_model();
    return model != nullptr
               ? model->visible_column_for_field(msqt::entry_field(msqt::EntryCellKind::start))
               : core::sheet::kNoIndex;
}

// Clicks the LIVE Fill button.  RibbonBar::set_active_tab() rebuilds the page
// and deletes its controls, so a pointer cached before another tab was shown
// is stale: find it again (and select the Task tab) on every click.
bool click_fill(msqt::MainWindow& window) {
    QAbstractButton* button = fill_button(window);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    return true;
}

std::size_t name_column(msqt::MainWindow& window) {
    auto* model = window.sheet_table_model();
    return model != nullptr
               ? model->visible_column_for_field(msqt::entry_field(msqt::EntryCellKind::name))
               : core::sheet::kNoIndex;
}

std::size_t row_for_uid(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return core::sheet::kNoIndex;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        if (sheet->row_uid(row) == uid) {
            return row;
        }
    }
    return core::sheet::kNoIndex;
}

std::string cell_text(msqt::MainWindow& window, std::size_t row, std::size_t column) {
    return window.sheet() != nullptr ? window.sheet()->cell_text(row, column) : std::string();
}

std::string start_text(msqt::MainWindow& window, std::uint32_t uid) {
    const std::size_t row = row_for_uid(window, uid);
    const std::size_t column = start_column(window);
    if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
        return std::string();
    }
    return cell_text(window, row, column);
}

bool select_start(msqt::MainWindow& window, std::uint32_t first_uid, std::uint32_t last_uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    const std::size_t first = row_for_uid(window, first_uid);
    const std::size_t last = row_for_uid(window, last_uid);
    const std::size_t column = start_column(window);
    if (sheet == nullptr || first == core::sheet::kNoIndex || last == core::sheet::kNoIndex ||
        column == core::sheet::kNoIndex) {
        return false;
    }
    return sheet->select_range(first, last, column, column);
}

void trigger(msqt::MainWindow& window, app::MenuAction id) {
    window.refresh_menu_state();
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, "the menu carries the action");
    if (action != nullptr) {
        action->trigger();
        QApplication::processEvents();
    }
}

// Toggles the Project Summary Task check box on the Gantt Chart Format tab and
// returns the live setting.  A view command, never a file write.
bool show_project_summary(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return false;
    }
    const int tab = ribbon->tab_titles().indexOf(QStringLiteral("Gantt Chart Format"));
    if (tab < 0) {
        return false;
    }
    ribbon->set_active_tab(tab);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int show_hide = groups.indexOf(QStringLiteral("Show/Hide"));
    if (show_hide < 0) {
        return false;
    }
    for (int control = 0; control < ribbon->group_control_count(show_hide); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(show_hide, control));
        if (button == nullptr || button->text() != QStringLiteral("Project Summary Task")) {
            continue;
        }
        if (!button->isChecked()) {
            button->click();
            QApplication::processEvents();
            QApplication::processEvents();
        }
        return window.project_summary_visible();
    }
    return false;
}

struct InputDialogResult {
    bool seen = false;
};

void fill_and_accept_input_dialog(InputDialogResult* seen, const QString& text) {
    QTimer::singleShot(0, [seen, text]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QInputDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible()) {
                continue;
            }
            seen->seen = true;
            dialog->setTextValue(text);
            dialog->accept();
            return;
        }
    });
}

std::set<std::uint32_t> uids_in_sheet(msqt::MainWindow& window) {
    std::set<std::uint32_t> uids;
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return uids;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        uids.insert(sheet->row_uid(row));
    }
    return uids;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_fill_gate <fixture.mpp> [out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, 800);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    core::sheet::SheetModel* sheet = window.sheet();
    gate::check(sheet != nullptr, "the fixture has a sheet");
    if (sheet == nullptr) {
        return gate::finish();
    }

    QAbstractButton* fill = fill_button(window);
    gate::check(fill != nullptr, "the Task tab's Editing group carries Fill");
    gate::check(fill != nullptr && fill->isEnabled(), "Fill is live (W30-1.8)");
    if (fill == nullptr) {
        return gate::finish();
    }
    {
        msqt::RibbonBar* ribbon = window.ribbon();
        const QStringList groups = ribbon->group_titles();
        const int editing = groups.indexOf(QStringLiteral("Editing"));
        bool found = false;
        for (int control = 0; control < ribbon->group_control_count(editing); ++control) {
            if (ribbon->group_control(editing, control) == fill) {
                found = ribbon->group_control_command(editing, control) ==
                        msqt::RibbonCommand::fill_down;
                break;
            }
        }
        gate::check(found, "the live button is wired to RibbonCommand::fill_down");
    }

    const std::size_t start = start_column(window);
    const std::size_t name = name_column(window);
    gate::check(start != core::sheet::kNoIndex, "the sheet has a Start column");
    gate::check(name != core::sheet::kNoIndex, "the sheet has a Task Name column");
    if (start == core::sheet::kNoIndex || name == core::sheet::kNoIndex) {
        return gate::finish();
    }

    // --- 2. refusals -------------------------------------------------------
    // (a) a non-Start column has no measured cell writer.
    {
        const std::size_t first = row_for_uid(window, 4);
        const std::size_t last = row_for_uid(window, 5);
        gate::check(first != core::sheet::kNoIndex && last != core::sheet::kNoIndex,
                    "uids 4 and 5 have sheet rows");
        if (first == core::sheet::kNoIndex || last == core::sheet::kNoIndex) {
            return gate::finish();
        }
        const std::string first_name = cell_text(window, first, name);
        const std::string last_name = cell_text(window, last, name);
        gate::check(sheet->select_range(first, last, name, name),
                    "a two-row Task Name selection is made");
        gate::check(click_fill(window), "the Fill button is still live");
        gate::check(cell_text(window, first, name) == first_name &&
                        cell_text(window, last, name) == last_name,
                    "Fill Down refuses a Task Name selection (no measured writer)");
        gate::check(window.pending_start_edits() == 0,
                    "the Name refusal stages no start edit");
        gate::check(!window.document_dirty(), "the Name refusal leaves the document clean");
        gate::check(!sheet->can_undo(), "the Name refusal records no undo step");
    }

    // (b) the source needs a row below it.
    {
        gate::check(select_start(window, 4, 4), "a one-row Start selection is made");
        const std::string four = start_text(window, 4);
        gate::check(click_fill(window), "the Fill button is still live");
        gate::check(start_text(window, 4) == four && !four.empty(),
                    "Fill Down refuses a one-row Start selection");
        gate::check(window.pending_start_edits() == 0 && !window.document_dirty(),
                    "the one-row refusal stages nothing");
        gate::check(!sheet->can_undo(), "the one-row refusal records no undo step");
    }

    // (c) the project summary row is read-only and can only be the source.
    {
        gate::check(show_project_summary(window), "the project summary row is shown");
        sheet = window.sheet();  // the toggle rebuilds the displayed rows
        gate::check(sheet != nullptr && row_for_uid(window, 0) == 0,
                    "the project summary row is row 0");
        const std::string one = start_text(window, 1);
        const std::size_t zero_row = row_for_uid(window, 0);
        const std::size_t one_row = row_for_uid(window, 1);
        gate::check(zero_row != core::sheet::kNoIndex && one_row != core::sheet::kNoIndex,
                    "the project and uid 1 rows exist");
        if (zero_row == core::sheet::kNoIndex || one_row == core::sheet::kNoIndex) {
            return gate::finish();
        }
        gate::check(sheet->select_range(zero_row, one_row, start, start),
                    "a project-row-to-task Start selection is made");
        gate::check(click_fill(window), "the Fill button is still live");
        gate::check(start_text(window, 1) == one,
                    "Fill Down refuses the project summary row as the source");
        gate::check(window.pending_start_edits() == 0 && !window.document_dirty(),
                    "the project-row refusal stages nothing");
        gate::check(!sheet->can_undo(), "the project-row refusal records no undo step");
    }

    // (d) a cleared source has no date to fill.
    {
        const std::size_t four_row = row_for_uid(window, 4);
        gate::check(four_row != core::sheet::kNoIndex, "uid 4 still has a row");
        if (four_row == core::sheet::kNoIndex) {
            return gate::finish();
        }
        const std::string four_before = start_text(window, 4);
        const std::string five_before = start_text(window, 5);
        gate::check(sheet->select_cell(four_row, start), "uid 4's Start cell is selected");
        trigger(window, app::MenuAction::cut);
        gate::check(start_text(window, 4).empty(), "Cut clears uid 4's Start");
        gate::check(select_start(window, 4, 5), "a cleared-source Start selection is made");
        gate::check(click_fill(window), "the Fill button is still live");
        gate::check(start_text(window, 5) == five_before,
                    "Fill Down refuses a top Start cell with no date");
        trigger(window, app::MenuAction::undo);
        gate::check(start_text(window, 4) == four_before,
                    "Undo restores the cut Start cell");
        gate::check(window.pending_start_edits() == 0 && !window.document_dirty(),
                    "the empty-source refusal and its undo leave the document clean");
    }

    // --- 3/4. the fill and its one-step undo -------------------------------
    {
        gate::check(select_start(window, 4, 6), "a uid 4..6 Start selection is made");
        const std::string source = start_text(window, 4);
        const std::string five_before = start_text(window, 5);
        const std::string six_before = start_text(window, 6);
        std::fprintf(stderr, "note: Start source '%s'; targets '%s' and '%s'\n",
                     source.c_str(), five_before.c_str(), six_before.c_str());
        gate::check(!source.empty() && source != five_before && source != six_before,
                    "the fill source differs from both targets");
        gate::check(click_fill(window), "the Fill button is still live");
        gate::check(start_text(window, 4) == source, "Fill Down leaves the source cell alone");
        gate::check(start_text(window, 5) == source, "Fill Down wrote uid 5's Start");
        gate::check(start_text(window, 6) == source, "Fill Down wrote uid 6's Start");
        gate::check(window.pending_start_edits() == 2,
                    "the fill is exactly two pending start edits");
        gate::check(window.document_dirty(), "the fill marks the document for saving");
        gate::check(sheet->can_undo(), "the fill records one undo step");

        trigger(window, app::MenuAction::undo);
        gate::check(start_text(window, 5) == five_before &&
                        start_text(window, 6) == six_before,
                    "ONE Undo restores both filled Start cells");
        gate::check(start_text(window, 4) == source, "Undo leaves the source cell alone");
        gate::check(window.pending_start_edits() == 0,
                    "Undo empties the pending start edits");
        gate::check(!window.document_dirty(), "Undo leaves the document clean");
    }

    // --- 5. the row menu runs the same path --------------------------------
    {
        gate::check(select_start(window, 4, 6), "another uid 4..6 Start selection is made");
        const std::string source = start_text(window, 4);
        const std::string five_before = start_text(window, 5);
        QMenu* menu = window.build_row_menu();
        QAction* row_fill = nullptr;
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Fill Down")) {
                row_fill = action;
            }
        }
        gate::check(row_fill != nullptr, "the row menu carries Fill Down");
        gate::check(row_fill != nullptr && row_fill->isEnabled(),
                    "the row menu's Fill Down is live (W30-1.8)");
        if (row_fill != nullptr) {
            row_fill->trigger();
            QApplication::processEvents();
            gate::check(start_text(window, 5) == source,
                        "the row menu's Fill Down wrote uid 5's Start");
            gate::check(window.pending_start_edits() == 2,
                        "the row menu's fill is two pending start edits");
            trigger(window, app::MenuAction::undo);
            gate::check(start_text(window, 5) == five_before && !window.document_dirty(),
                        "one Undo restores the row menu's fill");
        }
        delete menu;
    }

    // --- 6. a pending insert cannot be filled ------------------------------
    {
        msqt::MainWindow insert_window;
        insert_window.resize(msqt::kParityWidth, 800);
        QString insert_error;
        gate::check(insert_window.open_file(fixture, &insert_error),
                    "the fixture loads in the second window");
        insert_window.show();
        QApplication::processEvents();
        QApplication::processEvents();
        core::sheet::SheetModel* insert_sheet = insert_window.sheet();
        gate::check(insert_sheet != nullptr, "the second window has a sheet");
        if (insert_sheet != nullptr) {
            const std::set<std::uint32_t> before = uids_in_sheet(insert_window);
            gate::check(insert_sheet->select_cell(row_for_uid(insert_window, 6),
                                                 name_column(insert_window)),
                        "uid 6 is selected so the insert lands below it");
            InputDialogResult seen;
            fill_and_accept_input_dialog(&seen, QStringLiteral("Fill Pending Row"));
            trigger(insert_window, app::MenuAction::insert_task);
            gate::check(seen.seen, "Insert Task asks for a name");
            insert_sheet = insert_window.sheet();  // the insert rebuilds the rows
            std::uint32_t pending_uid = 0;
            for (std::uint32_t uid : uids_in_sheet(insert_window)) {
                if (before.find(uid) == before.end()) {
                    pending_uid = uid;
                }
            }
            gate::check(pending_uid != 0, "the pending insert has a displayed row");
            if (pending_uid != 0) {
                const std::string five_before = start_text(insert_window, 5);
                const std::string six_before = start_text(insert_window, 6);
                const std::string pending_before = start_text(insert_window, pending_uid);
                gate::check(select_start(insert_window, 4, pending_uid),
                            "a Start selection spans the pending row");
                QAbstractButton* insert_fill = fill_button(insert_window);
                gate::check(insert_fill != nullptr, "the second window has the Fill button");
                if (insert_fill != nullptr) {
                    insert_fill->click();
                    QApplication::processEvents();
                }
                gate::check(start_text(insert_window, 5) == five_before &&
                                start_text(insert_window, 6) == six_before,
                            "Fill Down refuses a selection with a pending insert");
                gate::check(start_text(insert_window, pending_uid) == pending_before,
                            "the pending row's Start is untouched");
                gate::check(insert_window.pending_start_edits() == 0,
                            "the pending-row refusal stages no start edit");
            }
        }
    }

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(window.grab().save(out_dir + QStringLiteral("/fill-down.png"), "PNG"),
                    "the filled view is written for human review");
    }

    return gate::finish();
}
