// app_qt/tests/outline_persist_test.cpp -- the View > Outline menu's result through
// the shell (wave 35, W35-2.3).
//
// apply_outline_level() is what the Outline menu's actions do.  Where the shape is
// one Project's wave35-viewstate-outline pairs measured (Level 1 or 2 on a measured
// outline that hides something) it queues a show_level edit that File > Save
// writes, and the session collapse is applied either way; every other level stays
// a session collapse and the status line names the reason.  All Subtasks
// withdraws a queued level.  The edit model's own rules and the bytes are proven
// by app_edits_show_level_tests and mppwrite_roundtrip_viewstate_outline.
//
// W35b-3.4 (coordinator mutation M13 on G2): the saved file is compared with Project's own
// save of the pair, stream by stream under the null-save discipline
// (tests/save_pair_compare.h), not merely checked for a queued edit.
//
//   qt_outline_persist_tests <wave35-viewstate-outline-level1-control.mpp>
//                            <wave35-viewstate-outline-deep-level1-control.mpp>
//                            <out-dir>
//                            <wave35-viewstate-outline-level1.mpp>
//                            <wave35-viewstate-outline-level1-control-null.mpp>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "dialogs.h"
#include "edit_model.h"
#include "edl_control_path.h"
#include "entry_columns.h"
#include "fixture_guard.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "sheet_table_model.h"
#include "shell_style.h"
#include "sort.h"

#include "gate_util.h"
#include "save_pair_compare.h"

namespace {

struct PairContext {
    void check(bool ok, const std::string& what) const { gate::check(ok, what.c_str()); }
    void expected_failure(bool unexpected_pass, const std::string& what) const {
        gate::check(false, ("unexpected KnownGap report: " + what +
                            (unexpected_pass ? " (gap gone)" : ""))
                               .c_str());
    }
};

const app::StructuralEdit* queued_level(const msqt::MainWindow& window) {
    const std::vector<app::StructuralEdit>& applied = window.pending_edits().applied();
    if (applied.size() != 1 || applied.front().kind != app::StructuralKind::show_level) {
        return nullptr;
    }
    return &applied.front();
}

bool status_contains(const msqt::MainWindow& window, const char* needle) {
    return window.status_left_text().contains(QString::fromUtf8(needle));
}

// W35f-1.8 (R35eW-1): drives Table > Date Format through its action; true when the dialog
// opened (and was accepted).  False when the shell refused before the dialog.
bool queue_date_format(msqt::MainWindow& window, int format) {
    bool seen = false;
    QTimer::singleShot(0, [&seen, format]() {
        auto* dialog =
            dynamic_cast<msqt::TableDateFormatDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        seen = true;
        const int index = dialog->date_format_combo()->findData(QVariant(format));
        if (index >= 0) {
            dialog->date_format_combo()->setCurrentIndex(index);
        }
        dialog->accept();
    });
    QAction* action = gate::find_action(window, app::MenuAction::table_date_format);
    if (action == nullptr || !action->isEnabled()) {
        return false;
    }
    action->trigger();
    QApplication::processEvents();
    return seen;
}

// True when some displayed row of the Name column reads `text`.
bool any_row_named(msqt::SheetTableModel* model, int name_column, const QString& text) {
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->data(model->index(row, name_column)).toString() == text) {
            return true;
        }
    }
    return false;
}

// The Gantt Chart Format tab's Project Summary Task check box (nullptr when absent).
QAbstractButton* project_summary_box(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int tab = ribbon->tab_titles().indexOf(QStringLiteral("Gantt Chart Format"));
    if (tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(tab);
    QApplication::processEvents();
    const int show_hide = ribbon->group_titles().indexOf(QStringLiteral("Show/Hide"));
    if (show_hide < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(show_hide); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(show_hide, control));
        if (button != nullptr && button->text() == QStringLiteral("Project Summary Task")) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 7) {
        std::fprintf(stderr,
                     "usage: qt_outline_persist_tests <five-task.mpp> <six-task.mpp> <out-dir> "
                     "<project-result.mpp> <control-null-save.mpp> <fat-slack.mpp>\n");
        return 2;
    }
    const QString five = QString::fromLocal8Bit(argv[1]);
    const QString six = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = QString::fromLocal8Bit(argv[3]);
    const std::string project_result = argv[4];
    const std::string null_save_path = argv[5];
    const QString read_only_file = QString::fromLocal8Bit(argv[6]);

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(five, &error), "the five-task fixture loads");
    window.show();
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "nothing is queued on open");
    const std::size_t full_rows = window.sheet_row_count();
    gate::check(full_rows >= 5, "the sheet shows every row on open");

    // Level 1 is a measured pair: it is queued, with the file's rows and levels.
    window.apply_outline_level(1);
    QApplication::processEvents();
    const app::StructuralEdit* level1 = queued_level(window);
    gate::check(level1 != nullptr, "Level 1 queues one show_level edit");
    if (level1 != nullptr) {
        gate::check(level1->show_level == 1, "the queued edit is Level 1");
        gate::check(level1->outline_uids == std::vector<std::uint32_t>({1, 2, 3, 4, 5}),
                    "the queued rows are the file's");
        gate::check(level1->outline_levels == std::vector<std::uint32_t>({0, 1, 1, 0, 0}),
                    "the queued levels are the file's");
    }
    gate::check(status_contains(window, "queued; File > Save writes it"),
                "the status line says the level is queued");

    // The session collapse is applied as well: the two children are hidden.
    gate::check(window.sheet_row_count() == full_rows - 2,
                "Level 1 hides the two children for the session");

    // All Subtasks withdraws the queued level and expands the session.
    window.apply_outline_level(0);
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "All Subtasks withdraws the queued level");
    gate::check(window.sheet_row_count() == full_rows, "All Subtasks shows every row again");
    gate::check(status_contains(window, "the queued level was withdrawn"),
                "the status line says the queued level was withdrawn");

    // W35f-1.8 (R35eW-1): Show Level is exclusive in the save, which refuses it beside a table /
    // date format edit.  The shell refuses at the change, in both orders.
    {
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(queued_level(window) != nullptr, "Level 1 is queued for the format case");
        gate::check(!queue_date_format(window, 5),
                    "Table / Date Format does not open beside a queued level");
        gate::check(queued_level(window) != nullptr, "the queued level stays queued");
        gate::check(status_contains(window,
                                    "a Show Level is queued and File > Save would write it, not "
                                    "a table / date format edit"),
                    "Table / Date Format beside a queued level: the status line names it");
        window.apply_outline_level(0);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "the format case: the level is withdrawn");

        // The other order, in a window of its own (a pending format edit cannot be discarded).
        msqt::MainWindow other;
        other.resize(msqt::kWindowWidth, msqt::kWindowHeight);
        QString other_error;
        gate::check(other.open_file(five, &other_error),
                    "the fixture loads for the format-first case");
        other.show();
        QApplication::processEvents();
        gate::check(queue_date_format(other, 5), "the date format is queued first");
        other.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(other.pending_edits().empty(),
                    "a level beside a pending date format queues nothing");
        gate::check(status_contains(other,
                                    "session only; the file keeps its outline (a table / date "
                                    "format or custom-field formula edit is pending"),
                    "a level beside a pending date format: the status line names it");
        gate::check(other.sheet_row_count() == full_rows - 2,
                    "the level still collapses the session");
    }

    // W35b-3.2 (R35G2-6): expanding or collapsing a row by hand while a level is
    // queued would leave the file describing rows the user is no longer looking at;
    // the toggle is refused, with the reason, and the queued level and the rows stay.
    window.apply_outline_level(1);
    QApplication::processEvents();
    gate::check(queued_level(window) != nullptr, "Level 1 is queued again for the toggle case");
    const std::size_t level1_rows = window.sheet_row_count();
    window.toggle_outline_row(0);
    QApplication::processEvents();
    gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                "a hand toggle leaves the queued Level 1 in place");
    gate::check(window.sheet_row_count() == level1_rows,
                "a hand toggle with a level queued changes no row");
    gate::check(status_contains(window, "a Show Level is queued and File > Save would write it"),
                "a hand toggle with a level queued: the status line names the queued level");
    // W35b-4.R7 (RW35-R7): Expand All is the same kind of change to the rows, so it is
    // refused with a level queued as well (Collapse All is enabled only while
    // something is expanded: its case is on the deep outline below).  Undo withdraws
    // the queued level and the rebuild shows every row again, so the display follows
    // the file (the session Sort is the one that outlives an Undo: qt_sort_persist).
    if (QAction* expand = gate::find_action(window, app::MenuAction::expand_all)) {
        gate::check(expand->isEnabled(), "Expand All is enabled while Level 1 hides rows");
        expand->trigger();
        QApplication::processEvents();
    } else {
        gate::check(false, "Expand All is a menu action");
    }
    gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                "Expand All with a level queued leaves the queued Level 1 in place");
    gate::check(window.sheet_row_count() == level1_rows,
                "Expand All with a level queued changes no row");
    gate::check(status_contains(window, "not this expanded state"),
                "Expand All with a level queued: the status line names the queued level");
    // W35c-1.1 (R35bG3-1): the Project Summary Task toggle rebuilds the sheet with every
    // row expanded, so it is refused while a level is queued, and the check box is put
    // back as it was.
    {
        QAbstractButton* summary = project_summary_box(window);
        gate::check(summary != nullptr, "the Project Summary Task check box is in the page");
        if (summary != nullptr) {
            const bool shown_before = window.project_summary_visible();
            summary->click();
            QApplication::processEvents();
            gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                        "the summary toggle leaves the queued Level 1 in place");
            gate::check(window.sheet_row_count() == level1_rows,
                        "the summary toggle with a level queued changes no row");
            gate::check(window.project_summary_visible() == shown_before,
                        "the summary toggle with a level queued leaves the summary as it was");
            gate::check(summary->isChecked() == shown_before,
                        "the summary toggle with a level queued puts the check box back");
            gate::check(status_contains(window, "not the project summary row shown or hidden"),
                        "the summary toggle with a level queued: the status line names the "
                        "queued level");
        }
    }
    if (QAction* undo = gate::find_action(window, app::MenuAction::undo)) {
        undo->trigger();
        QApplication::processEvents();
    }
    gate::check(window.pending_edits().empty(), "Undo withdraws the queued level");
    gate::check(window.sheet_row_count() == full_rows,
                "Undo of a queued level shows every row, as the file will be saved");
    // W35c-1.2 (R35bG4-1): Redo after that Undo queues the level again, so the sheet must
    // show the level again; before the fix it showed every row while Save wrote Level 1.
    {
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(redo != nullptr && redo->isEnabled(),
                    "Redo is enabled after the Undo of a queued level");
        if (redo != nullptr) {
            redo->trigger();
            QApplication::processEvents();
            gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                        "Redo queues Level 1 again");
            gate::check(window.sheet_row_count() == level1_rows,
                        "Redo of a queued level shows the level it queued");
            gate::check(status_contains(window, "redo the queued Show Level"),
                        "Redo of a queued level: the status line names the level");

            // All Subtasks withdraws the level, and the withdrawn level cannot come back.
            window.apply_outline_level(0);
            QApplication::processEvents();
            gate::check(!redo->isEnabled(), "All Subtasks leaves nothing to redo");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Redo after All Subtasks queues nothing");
            gate::check(window.sheet_row_count() == full_rows,
                        "Redo after All Subtasks leaves every row shown");

            // A level the builder refuses replaces the queued one for the session, and
            // the withdrawn Level 1 must not come back under the new display.
            window.apply_outline_level(1);
            QApplication::processEvents();
            window.apply_outline_level(3);
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "a refused replacement level withdraws the queued one");
            gate::check(!redo->isEnabled(), "a refused replacement level leaves nothing to redo");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Redo after a refused replacement level queues nothing");
            gate::check(window.sheet_row_count() == full_rows,
                        "Redo after a refused replacement level leaves the display as it was");
        }
    }
    // With nothing queued the same toggle works (All Subtasks first withdraws it).
    window.apply_outline_level(0);
    QApplication::processEvents();
    window.toggle_outline_row(0);
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "a hand toggle queues nothing");
    gate::check(window.sheet_row_count() == full_rows - 2,
                "a hand toggle with nothing queued collapses the summary's two children");
    window.apply_outline_level(0);
    QApplication::processEvents();
    gate::check(window.sheet_row_count() == full_rows, "All Subtasks shows every row again (2)");

    // Level 2 on this outline hides nothing: session only, with the reason.
    window.apply_outline_level(2);
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "Level 2 hides nothing: nothing is queued");
    gate::check(status_contains(window, "hides nothing on this outline"),
                "Level 2: the status line names the reason");
    gate::check(status_contains(window, "session only"),
                "Level 2: the status line says session only");

    // Levels beyond the measured ones stay session only, each with its reason.
    for (const int level : {3, 4, 9}) {
        window.apply_outline_level(level);
        QApplication::processEvents();
        const std::string label = "Level " + std::to_string(level);
        gate::check(window.pending_edits().empty(), (label + ": nothing is queued").c_str());
        gate::check(status_contains(window, ("Show Level " + std::to_string(level) +
                                             " is not implemented in this port: the measured "
                                             "Project pairs show Level 1 and Level 2")
                                                .c_str()),
                    (label + ": the status line names the reason").c_str());
        gate::check(status_contains(window, "session only"),
                    (label + ": the status line says session only").c_str());
    }

    // The six-task three-level outline: Level 2 replaces a queued Level 1.
    gate::check(window.open_file(six, &error), "the six-task fixture loads");
    QApplication::processEvents();
    window.apply_outline_level(1);
    QApplication::processEvents();
    const app::StructuralEdit* deep1 = queued_level(window);
    gate::check(deep1 != nullptr && deep1->show_level == 1, "Level 1 queues on the deep outline");
    window.apply_outline_level(2);
    QApplication::processEvents();
    const app::StructuralEdit* deep2 = queued_level(window);
    gate::check(deep2 != nullptr && deep2->show_level == 2,
                "Level 2 replaces the queued Level 1 (Show Level is exclusive)");

    // W35b-4.R7 (RW35-R7): Level 2 leaves the level-2 summary expanded, so Collapse All
    // is enabled; with the level queued it is refused, the rows and the edit stay.
    const std::size_t deep_level2_rows = window.sheet_row_count();
    if (QAction* collapse = gate::find_action(window, app::MenuAction::collapse_all)) {
        gate::check(collapse->isEnabled(), "Collapse All is enabled while Level 2 leaves a row expanded");
        collapse->trigger();
        QApplication::processEvents();
    } else {
        gate::check(false, "Collapse All is a menu action");
    }
    gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 2,
                "Collapse All with a level queued leaves the queued Level 2 in place");
    gate::check(window.sheet_row_count() == deep_level2_rows,
                "Collapse All with a level queued changes no row");
    gate::check(status_contains(window, "not this collapsed state"),
                "Collapse All with a level queued: the status line names the queued level");

    // W35c-1.3 (R35bW-1): a grid edit made AFTER a Show Level is queued is refused with the
    // queued level named, and so is the sheet's Redo of an earlier one.
    {
        gate::check(window.open_file(five, &error), "the five-task fixture reopens (later edits)");
        QApplication::processEvents();
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(queued_level(window) != nullptr, "Level 1 is queued for the later-edit gate");
        const std::size_t queued_rows = window.sheet_row_count();
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        const int duration_column = msqt::entry_column_index(msqt::EntryCellKind::duration);
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(name_column >= 0 && duration_column >= 0, "the Entry table has Name and Duration");
        const QString name_before = model->data(model->index(1, name_column)).toString();
        gate::check(!model->setData(model->index(1, name_column),
                                    QStringLiteral("Renamed after the level"), Qt::EditRole),
                    "a rename after a queued level is refused");
        gate::check(model->data(model->index(1, name_column)).toString() == name_before,
                    "the refused rename leaves the name as it was");
        gate::check(status_contains(window, "a Show Level is queued and File > Save would write "
                                            "it, not an edited cell"),
                    "a rename after a queued level: the status line names the queued level");
        gate::check(!model->setData(model->index(1, duration_column), QStringLiteral("9d"),
                                    Qt::EditRole),
                    "a Duration edit after a queued level is refused");
        window.sheet()->select_cell(1, 0);
        msqt::TaskInformationValues values;
        gate::check(window.task_information_values(&values), "a task is selected for the form");
        values.name = "Renamed in the form";
        gate::check(!window.apply_task_information(values),
                    "a task form edit after a queued level is refused");
        gate::check(status_contains(window, "not an edited task field"),
                    "a task form edit after a queued level: the status line names the level");
        gate::check(queued_level(window) != nullptr && window.sheet_row_count() == queued_rows,
                    "the refused edits leave the level queued and the rows as they were");

        // W35d-1.1 (R35cG1-3): Paste writes through SheetModel::paste, not setData, so the cell
        // gate does not cover it; refuse_view_change("a pasted cell") is its only guard.  Copy
        // (which changes nothing) fills the window's own clipboard, then Paste onto another
        // Name cell under the queued level.
        QAction* copy = gate::find_action(window, app::MenuAction::copy);
        QAction* paste = gate::find_action(window, app::MenuAction::paste);
        gate::check(copy != nullptr && paste != nullptr && queued_rows >= 3,
                    "Copy and Paste exist and the queued level shows three rows");
        if (copy != nullptr && paste != nullptr && queued_rows >= 3) {
            window.sheet()->select_cell(2, static_cast<std::size_t>(name_column));
            window.refresh_menu_state();
            QApplication::processEvents();
            copy->trigger();
            QApplication::processEvents();
            window.sheet()->select_cell(1, static_cast<std::size_t>(name_column));
            window.refresh_menu_state();
            QApplication::processEvents();
            gate::check(paste->isEnabled(), "Paste is enabled on the selected cell");
            paste->trigger();
            QApplication::processEvents();
            gate::check(status_contains(window, "a Show Level is queued and File > Save would write "
                                                "it, not a pasted cell"),
                        "a paste after a queued level: the status line names the queued level");
            gate::check(model->data(model->index(1, name_column)).toString() == name_before,
                        "the refused paste leaves the name as it was");
            gate::check(queued_level(window) != nullptr && window.sheet_row_count() == queued_rows,
                        "the refused paste leaves the level queued and the rows as they were");
        }

        // W35d-1.2 (R35cG1-4): a session Sort chosen while a level is queued is refused with the
        // level named (the same guard as the other changes to what Save writes): the display
        // would re-order rows while Save wrote the level over the file's own order.  Clearing the
        // sort is not refused.
        {
            core::view::SortDefinition by_name;
            by_name.id = "app.sort";
            by_name.name = "Name descending";
            by_name.keys.push_back(core::view::SortKey{app::task_field_id("Name"), false});
            const std::vector<QString> names_before = [&]() {
                std::vector<QString> names;
                for (int row = 0; row < model->rowCount(); ++row) {
                    names.push_back(model->data(model->index(row, name_column)).toString());
                }
                return names;
            }();
            window.apply_sort_choice(by_name, "Name descending", false);
            QApplication::processEvents();
            gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1 &&
                            window.sheet_row_count() == queued_rows,
                        "a session Sort leaves the queued level and the rows as they were");
            gate::check(status_contains(window, "a Show Level is queued and File > Save would write "
                                                "it, not a session Sort"),
                        "a session Sort after a queued level: the status line names the level");
            std::vector<QString> names_after;
            for (int row = 0; row < model->rowCount(); ++row) {
                names_after.push_back(model->data(model->index(row, name_column)).toString());
            }
            gate::check(names_after == names_before, "the refused Sort reorders no row");
            window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            QApplication::processEvents();
            gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                        "clearing the sort leaves the queued level in place");
            gate::check(!status_contains(window, "not a session Sort"),
                        "clearing the sort under a queued level is not refused");

            // W35e-2.3 (R35dG1-5): the clear above runs with no sort shown (the Sort just above
            // was refused), so it proves little.  Both routes the finding names are closed since
            // W35e-1.1, so a sorted display under a queued level is reached through the session
            // entry point the gates and the header use (apply_session_sort has no queued-view
            // guard of its own; apply_sort_choice is the production caller and refuses first).
            // Clearing that sort must show the file's order again and leave the level, its
            // rows and its collapse state alone.
            window.apply_session_sort(by_name, "Name descending");
            QApplication::processEvents();
            gate::check(window.sort_active() && queued_level(window) != nullptr,
                        "a sorted display under a queued level is reached (session entry point)");
            gate::check(window.sheet_row_count() == queued_rows,
                        "the sorted display under a queued level keeps the level's rows");
            window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            QApplication::processEvents();
            gate::check(!window.sort_active(), "clearing the sort under a queued level clears it");
            gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                        "clearing a shown sort leaves the queued level in place");
            gate::check(window.sheet_row_count() == queued_rows,
                        "clearing a shown sort leaves the queued level's rows");
            std::vector<QString> names_cleared;
            for (int row = 0; row < model->rowCount(); ++row) {
                names_cleared.push_back(model->data(model->index(row, name_column)).toString());
            }
            gate::check(names_cleared == names_before,
                        "clearing a shown sort shows the file's order again");
        }

        // Undo in the sheet, then the level, then Ctrl+Y: queueing the level rebuilds the
        // document with a sheet that has no history, so there is no Redo to re-apply the
        // rename under it.  Pinned.
        gate::check(window.open_file(five, &error), "the five-task fixture reopens (sheet redo)");
        QApplication::processEvents();
        model = window.sheet_table_model();
        gate::check(model->setData(model->index(1, name_column),
                                   QStringLiteral("Renamed then undone"), Qt::EditRole),
                    "a rename commits with nothing queued");
        if (QAction* undo = gate::find_action(window, app::MenuAction::undo)) {
            undo->trigger();
            QApplication::processEvents();
        }
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(queued_level(window) != nullptr,
                    "Level 1 queues after the sheet's Undo (the plan is clean)");
        if (QAction* redo = gate::find_action(window, app::MenuAction::redo)) {
            gate::check(!redo->isEnabled(), "the queued level's rebuild leaves no sheet Redo");
            redo->trigger();
            QApplication::processEvents();
        } else {
            gate::check(false, "Redo is a menu action");
        }
        gate::check(!any_row_named(model, name_column, QStringLiteral("Renamed then undone")),
                    "Redo does not re-apply the rename under the queued level");
        gate::check(queued_level(window) != nullptr, "Redo leaves the level queued");
    }

    // W35b-1.5 (RW35-R2): a grid edit that is not saved yet.  Show Level is built
    // from the session plan, which carries the rename; the builder refuses it,
    // nothing is queued, and the status line says to save or undo first.
    {
        gate::check(window.open_file(five, &error), "the five-task fixture reopens");
        QApplication::processEvents();
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        gate::check(name_column >= 0, "the Entry table has a Name column");
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(name_column >= 0 &&
                        model->setData(model->index(1, name_column),
                                       QStringLiteral("Renamed in the grid"), Qt::EditRole),
                    "a Name cell edit commits through the shell's model path");
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "unsaved rename: Level 1 queues nothing");
        gate::check(status_contains(window, "Show Level is not implemented in this port while "
                                            "the plan carries unsaved edits"),
                    "unsaved rename: the status line names the unsaved edits");
        gate::check(status_contains(window, "(or save them, where Save writes that field)"),
                    "unsaved rename: the status line says undo the grid edits first (or save them)");
        gate::check(status_contains(window, "session only"),
                    "unsaved rename: the status line says session only");
    }

    // W35d-0.4 (R35cG1-1, Show Level counterpart): Level 1, Ctrl+Z, then All Subtasks or a
    // session-only level, then Ctrl+Y.  apply_outline_level() only clears the redo list when a
    // level is queued at that moment, so the withdrawn level stays redoable; unlike a Sort it
    // comes back SHOWN, because every rebuild re-shows a queued level (W35c-1.2).  Pinned as
    // the invariant that matters: whatever Redo queues, the rows show that level.  This route
    // does not reproduce the Sort's divergence, so no code changed for it.
    {
        gate::check(window.open_file(five, &error), "the five-task fixture reopens (undo first)");
        QApplication::processEvents();
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(undo != nullptr && redo != nullptr, "Undo and Redo are menu actions");
        if (undo != nullptr && redo != nullptr) {
            const std::size_t expanded_rows = window.sheet_row_count();
            for (const int other : {0, 3}) {
                window.apply_outline_level(1);
                QApplication::processEvents();
                gate::check(queued_level(window) != nullptr, "Undo-first: Level 1 is queued");
                const std::size_t queued_level_rows = window.sheet_row_count();
                gate::check(queued_level_rows < expanded_rows, "Undo-first: Level 1 hides rows on this outline");
                undo->trigger();
                QApplication::processEvents();
                gate::check(window.pending_edits().empty() && redo->isEnabled(),
                            "Undo-first: Undo withdraws Level 1 onto the redo list");
                window.apply_outline_level(other);
                QApplication::processEvents();
                gate::check(window.pending_edits().empty(),
                            "Undo-first: All Subtasks or Level 3 queues nothing");
                const std::size_t replaced_rows = window.sheet_row_count();
                if (redo->isEnabled()) {
                    redo->trigger();
                    QApplication::processEvents();
                }
                gate::check(queued_level(window) == nullptr ||
                                window.sheet_row_count() == queued_level_rows,
                            "Undo-first: a Redo that re-queues Level 1 shows Level 1's rows");
                gate::check(queued_level(window) != nullptr ||
                                window.sheet_row_count() == replaced_rows,
                            "Undo-first: no Redo leaves the rows as they were");
            }
        }
    }

    // W35d-0.5 (R35cG1-2): Cut and Paste end the structural redo list.  Route: Level 1,
    // Ctrl+Z (nothing queued, so the cell gate is open), a cut or pasted Name, Ctrl+Y.  Left on
    // the list, Redo re-queued the level over the unsaved name without push() or the builder.
    {
        gate::check(window.open_file(five, &error), "the five-task fixture reopens (cut and paste)");
        QApplication::processEvents();
        msqt::SheetTableModel* model = window.sheet_table_model();
        const std::size_t name_column =
            model->visible_column_for_field(app::task_field_id("Name"));
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        QAction* cut = gate::find_action(window, app::MenuAction::cut);
        QAction* copy = gate::find_action(window, app::MenuAction::copy);
        QAction* paste = gate::find_action(window, app::MenuAction::paste);
        gate::check(undo != nullptr && redo != nullptr && cut != nullptr && copy != nullptr &&
                        paste != nullptr && name_column != core::sheet::kNoIndex,
                    "Undo, Redo, Cut, Copy, Paste and a Name column exist");
        if (undo != nullptr && redo != nullptr && cut != nullptr && copy != nullptr &&
            paste != nullptr && name_column != core::sheet::kNoIndex) {
            for (const bool pasting : {false, true}) {
                const std::string route = pasting ? "Paste" : "Cut";
                window.apply_outline_level(1);
                QApplication::processEvents();
                gate::check(queued_level(window) != nullptr,
                            (route + ": Level 1 is queued").c_str());
                undo->trigger();
                QApplication::processEvents();
                gate::check(window.pending_edits().empty() && redo->isEnabled(),
                            (route + ": Undo withdraws Level 1 onto the redo list").c_str());
                const int name_col = static_cast<int>(name_column);
                const auto names = [&]() {
                    std::vector<QString> all;
                    for (int row = 0; row < model->rowCount(); ++row) {
                        all.push_back(model->data(model->index(row, name_col)).toString());
                    }
                    return all;
                };
                if (pasting) {
                    window.sheet()->select_cell(2, name_column);
                    window.refresh_menu_state();
                    QApplication::processEvents();
                    copy->trigger();
                    QApplication::processEvents();
                }
                window.sheet()->select_cell(1, name_column);
                window.refresh_menu_state();
                QApplication::processEvents();
                gate::check(pasting ? paste->isEnabled() : cut->isEnabled(),
                            (route + " is enabled on the Name cell").c_str());
                (pasting ? paste : cut)->trigger();
                QApplication::processEvents();
                gate::check(status_contains(window, pasting ? "pasted 1 changed cell(s)"
                                                            : "cut 1x1 cell(s), values only"),
                            (route + " is applied to the Name cell").c_str());
                const std::vector<QString> edited = names();
                gate::check(!redo->isEnabled(),
                            (route + " ends the structural redo list (Redo is disabled)").c_str());
                redo->trigger();
                QApplication::processEvents();
                gate::check(window.pending_edits().empty(),
                            (route + ": Redo re-queues no level over the edited name").c_str());
                gate::check(names() == edited,
                            (route + ": Redo leaves the edited names as they were").c_str());
                gate::check(window.open_file(five, &error),
                            (route + ": the fixture reopens").c_str());
                QApplication::processEvents();
                model = window.sheet_table_model();
            }
        }
    }

    // W35e-0.1 (R35dG0-1): a PARTLY applied Task Information or Task Form edit ends the structural
    // redo list.  Route: Level 1, Ctrl+Z, a Name that commits and a Duration that does not parse,
    // Ctrl+Y.  The Name stays committed, and left on the list Redo re-queued the level over the
    // unsaved name without push() or the builder.
    {
        gate::check(window.open_file(five, &error), "the five-task fixture reopens (partial apply)");
        QApplication::processEvents();
        const int name_col = msqt::entry_column_index(msqt::EntryCellKind::name);
        msqt::SheetTableModel* model = window.sheet_table_model();
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(undo != nullptr && redo != nullptr && name_col >= 0,
                    "Undo, Redo and a Name column exist (partial apply)");
        if (undo != nullptr && redo != nullptr && name_col >= 0) {
            window.apply_outline_level(1);
            QApplication::processEvents();
            gate::check(queued_level(window) != nullptr, "partial apply: Level 1 is queued");
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        "partial apply: Undo withdraws Level 1 onto the redo list");
            window.sheet()->select_cell(1, 0);
            msqt::TaskInformationValues values;
            gate::check(window.task_information_values(&values),
                        "partial apply: a task is selected for the form");
            values.name = "Renamed by a partial apply";
            values.duration_text = "not a duration";
            gate::check(!window.apply_task_information(values),
                        "partial apply: the form edit fails on the Duration");
            gate::check(any_row_named(model, name_col, QStringLiteral("Renamed by a partial apply")),
                        "partial apply: the Name field stays committed");
            gate::check(!redo->isEnabled(),
                        "partial apply: the committed Name ends the structural redo list "
                        "(Redo is disabled)");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "partial apply: Redo re-queues no level over the edited name");
            gate::check(any_row_named(model, name_col, QStringLiteral("Renamed by a partial apply")),
                        "partial apply: Redo leaves the edited name as it was");
        }
    }

    // W35e-1.1 (R35dG1-1): the two bypasses of the 1.2 cross-kind rule.  The needles are the
    // queue, the redo state and the displayed rows, not a status text alone.
    {
        core::view::SortDefinition by_name;
        by_name.id = "app.sort";
        by_name.name = "Name descending";
        by_name.keys.push_back(core::view::SortKey{app::task_field_id("Name"), false});
        const int name_col = msqt::entry_column_index(msqt::EntryCellKind::name);
        const auto shown_names = [&](msqt::SheetTableModel* model) {
            std::vector<QString> names;
            for (int row = 0; row < model->rowCount(); ++row) {
                names.push_back(model->data(model->index(row, name_col)).toString());
            }
            return names;
        };
        const auto is_subsequence = [](const std::vector<QString>& part,
                                       const std::vector<QString>& whole) {
            std::size_t next = 0;
            for (const QString& name : part) {
                while (next < whole.size() && whole[next] != name) {
                    ++next;
                }
                if (next == whole.size()) {
                    return false;
                }
                ++next;
            }
            return true;
        };

        // (a) A session Sort first, then a Level: the Level must not be queued over the sorted
        // rows.  Clearing the sort lets the same Level queue.
        gate::check(window.open_file(six, &error), "the six-task fixture reopens (sort first)");
        QApplication::processEvents();
        msqt::SheetTableModel* model = window.sheet_table_model();
        window.apply_sort_choice(by_name, "Name descending", false);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "sort first: the six-task Sort stays session only");
        const std::vector<QString> sorted_names = shown_names(model);
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(),
                    "sort first: a Level under a session Sort queues nothing");
        // W35f-0.1 (R35eG1-1): only the QUEUE is refused.  The Level is shown for the session,
        // over the sorted rows (a subsequence of them), and the note names the shown sort.
        gate::check(status_contains(window, "session only; a session Sort is shown and File > Save "
                                            "would write the file's own order, so the level is "
                                            "not queued"),
                    "sort first: the status line names the shown sort and the refused queue");
        const std::vector<QString> level_names = shown_names(model);
        gate::check(level_names.size() < sorted_names.size() &&
                        is_subsequence(level_names, sorted_names),
                    "sort first: the session Level is shown, over the sorted rows in the sorted "
                    "order");
        window.apply_outline_level(0);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty() &&
                        !status_contains(window, "a session Sort is shown"),
                    "sort first: All Subtasks is not refused and queues nothing");
        gate::check(shown_names(model) == sorted_names,
                    "sort first: All Subtasks shows every sorted row again");
        // A Level that could never be queued (Level 3 to 9 is not a measured shape) is a
        // session-only Level with its own note, as in wave 31, not the sort's refusal.
        window.apply_outline_level(3);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty() &&
                        status_contains(window, "session only; the file keeps its outline") &&
                        !status_contains(window, "so the level is not queued"),
                    "sort first: an unmeasured Level under a session Sort is shown for the session "
                    "with the builder's note");
        gate::check(is_subsequence(shown_names(model), sorted_names),
                    "sort first: the unmeasured session Level keeps the sorted order");
        window.apply_outline_level(0);
        QApplication::processEvents();
        window.apply_sort_choice(core::view::SortDefinition{}, "", true);
        QApplication::processEvents();
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                    "sort first: with the sort cleared the same Level queues");

        // (b) A queued Level, Ctrl+Z, a sort choice, then Ctrl+Y: the sort ends the Level on the
        // redo list, so Redo queues nothing under the sorted display.
        gate::check(window.open_file(six, &error), "the six-task fixture reopens (redo then sort)");
        QApplication::processEvents();
        model = window.sheet_table_model();
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(undo != nullptr && redo != nullptr, "Undo and Redo exist (redo then sort)");
        if (undo != nullptr && redo != nullptr) {
            window.apply_outline_level(1);
            QApplication::processEvents();
            gate::check(queued_level(window) != nullptr, "redo then sort: Level 1 is queued");
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        "redo then sort: Undo withdraws Level 1 onto the redo list");
            window.apply_sort_choice(by_name, "Name descending", false);
            QApplication::processEvents();
            const std::vector<QString> resorted = shown_names(model);
            gate::check(!redo->isEnabled(),
                        "redo then sort: a sort choice ends the Show Level on the redo list");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "redo then sort: Ctrl+Y queues no level under the sorted display");
            gate::check(shown_names(model) == resorted,
                        "redo then sort: Ctrl+Y leaves the sorted rows as they were");
        }

        // (c) W35f-1.4 (R35eG1-4): a Level on the redo list, then the sort is CLEARED.  Clearing
        // does not change the order Save writes (the file's own), so the Redo that matches the
        // display stays: it re-queues the Level relative to the file order, which is shown.
        // Both cleared paths: the dialog's [Clear] and the plain clear.
        for (int path = 0; path < 2; ++path) {
            const std::string label = path == 0 ? "cleared (dialog path)" : "cleared (plain path)";
            gate::check(window.open_file(six, &error), "the six-task fixture reopens (redo then clear)");
            QApplication::processEvents();
            QAction* undo_c = gate::find_action(window, app::MenuAction::undo);
            QAction* redo_c = gate::find_action(window, app::MenuAction::redo);
            if (undo_c == nullptr || redo_c == nullptr) {
                gate::check(false, "Undo and Redo exist (redo then clear)");
                break;
            }
            window.apply_outline_level(1);
            QApplication::processEvents();
            undo_c->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo_c->isEnabled(),
                        ("redo then " + label + ": Undo withdraws Level 1 onto the redo list").c_str());
            if (path == 0) {
                window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            } else {
                window.clear_session_sort();
            }
            QApplication::processEvents();
            gate::check(redo_c->isEnabled(),
                        ("redo then " + label + ": the Level stays on the redo list").c_str());
            redo_c->trigger();
            QApplication::processEvents();
            gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                        ("redo then " + label + ": Ctrl+Y re-queues the Level the display shows")
                            .c_str());
        }
    }

    // W35g-1.1 (R35fG0-5): the read-only case of W35f-0.1.  A read-only file shows a session Sort,
    // and a Level chosen under it names the read-only reason, not the shown sort's refusal.  The
    // read-only check must stay before the sort check in apply_outline_level.  The file is only
    // opened and never saved.
    {
        core::view::SortDefinition by_name;
        by_name.id = "app.sort";
        by_name.name = "Name descending";
        by_name.keys.push_back(core::view::SortKey{app::task_field_id("Name"), false});
        gate::check(window.open_file(read_only_file, &error), "the read-only fixture opens");
        QApplication::processEvents();
        gate::check(status_contains(window, "saving is not supported yet"),
                    "read-only: the status line names the read-only reason on opening");
        window.apply_sort_choice(by_name, "Name descending", false);
        QApplication::processEvents();
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "read-only: a Level under a Sort queues nothing");
        gate::check(status_contains(window, "outline: level 1 (session only; saved by Project with "
                                            "unreferenced sectors"),
                    "read-only: the Level's note is the read-only reason");
        gate::check(!status_contains(window, "so the level is not queued"),
                    "read-only: the shown-sort refusal is not what the note says");
    }

    // W35b-3.4: Level 1 on the five-task control, saved through File > Save, is Project's own
    // save of the pair (outline-level1), every stream under the null-save discipline.  The
    // same bytes the core and app tests pin, here through the shell's queue and save path.
    {
        std::error_code file_error;
        std::filesystem::create_directories(out_dir.toStdString(), file_error);
        const QString saved_path = out_dir + QStringLiteral("/w35b-qt-outline-level1-saved.mpp");
        std::filesystem::remove(saved_path.toStdString(), file_error);
        gate::check(window.open_file(five, &error), "the five-task fixture reopens for the save");
        QApplication::processEvents();
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                    "Level 1 is queued for the save");
        QString save_error;
        const bool saved = window.save_document_path(saved_path, &save_error);
        gate::check(saved, "the queued Level 1 saves");
        if (!saved) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
        } else {
            try {
                fixture::require_committed(project_result);
                fixture::require_committed(null_save_path);
                const mppwrite::CfbDocument control_doc =
                    mppwrite::CfbDocument::from_file(five.toStdString());
                const std::size_t control_path_bytes = edl_control_path::path_record_bytes(
                    control_doc.read_stream(edl_control_path::kEdlVar2));
                const mppwrite::CfbDocument null_doc =
                    edl_control_path::with_control_path_size(null_save_path, control_path_bytes);
                const mppwrite::CfbDocument project_doc =
                    edl_control_path::with_control_path_size(project_result, control_path_bytes);
                const mppwrite::CfbDocument saved_doc =
                    mppwrite::CfbDocument::from_file(saved_path.toStdString());
                PairContext context;
                const std::array<const char*, 5> declared{
                    {"Props14", "114/TBkndTask/FixedMeta", "214/CEdl/Var2Data",
                     "214/CEdl/VarMeta", "214/Props"}};
                save_pair::compare_all(context, "qt outline level1", control_doc, null_doc,
                                       project_doc, saved_doc, nullptr, 0, declared.data(),
                                       declared.size());
            } catch (const std::exception& pair_error) {
                std::fprintf(stderr, "note: pair comparison error: %s\n", pair_error.what());
                gate::check(false, "the saved file is compared with Project's pair");
            }
        }
    }

    std::printf("qt_outline_persist: ");
    return gate::finish();
}
