// app_qt/tests/sort_persist_test.cpp -- the Sort dialog's result through the shell
// (wave 35, W35-2.1).
//
// apply_sort_choice() is what show_sort_dialog() does with the dialog's accepted
// result.  Where the shape is one Project's wave35-viewstate-sort pairs measured
// (one Name key) it queues a sort_view edit that File > Save writes; every other
// shape stays a session sort and the status line names the reason.  The edit
// model's own rules and the bytes are proven by app_edits_sort_tests and
// mppwrite_roundtrip_viewstate_sort.
//
// W35b-3.4 (coordinator mutation M7 on G2: the Qt gate went red only because queueing failed):
// the saved file is compared with Project's own save of the pair, stream by stream under the
// null-save discipline (tests/save_pair_compare.h).
//
//   qt_sort_persist_tests <wave35-viewstate-sort-name-desc-control.mpp>
//                         <out-dir>
//                         <wave35-viewstate-sort-name-desc.mpp>
//                         <wave35-viewstate-sort-name-desc-control-null.mpp>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
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
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "dialogs.h"
#include "edit_model.h"
#include "entry_columns.h"
#include "fixture_guard.h"
#include "layout_model.h"
#include "main_window.h"
#include "project_dialogs_qt.h"
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

core::view::SortDefinition one_key_sort(const char* field, bool ascending, const char* name) {
    core::view::SortDefinition sort;
    sort.id = "app.sort";
    sort.name = name;
    sort.keys.push_back(core::view::SortKey{app::task_field_id(field), ascending});
    return sort;
}

const app::StructuralEdit* queued_sort(const msqt::MainWindow& window) {
    const std::vector<app::StructuralEdit>& applied = window.pending_edits().applied();
    if (applied.size() != 1 || applied.front().kind != app::StructuralKind::sort_view) {
        return nullptr;
    }
    return &applied.front();
}

// The Name column of the displayed rows, in display order.
std::vector<QString> displayed_names(msqt::SheetTableModel* model, int name_column) {
    std::vector<QString> names;
    for (int row = 0; row < model->rowCount(); ++row) {
        names.push_back(model->data(model->index(row, name_column)).toString());
    }
    return names;
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

// W35g-0.1 (R35fG1-1): the nested input dialog Formula... opens.  It is accepted with a formula
// when it shows (and `seen` set), so a missing refusal queues an edit and the gate fails on it.
// It polls and gives up after `tries` rounds, so it cannot hang the gate.
void accept_input_dialog(bool* seen, int tries) {
    QTimer::singleShot(10, [seen, tries]() {
        if (auto* input = dynamic_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            *seen = true;
            input->setTextValue(QStringLiteral("[Duration]/240"));
            input->accept();
        } else if (tries > 0) {
            accept_input_dialog(seen, tries - 1);
        }
    });
}

struct CustomFieldsRun {
    bool dialog_seen = false;  // Project > Custom Fields... opened
    bool input_seen = false;   // Formula... opened its input dialog
};

// Drives Project > Custom Fields...: the dialog is cancelled, or Formula... is used on Text1 and
// the dialog accepted.
CustomFieldsRun run_custom_fields(msqt::MainWindow& window, bool use_formula) {
    CustomFieldsRun run;
    QTimer::singleShot(0, [&run, use_formula]() {
        auto* dialog = dynamic_cast<msqt::CustomFieldsDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        run.dialog_seen = true;
        if (!use_formula) {
            dialog->reject();
            return;
        }
        for (int row = 0; row < dialog->field_list()->count(); ++row) {
            if (dialog->field_list()->item(row)->text() == QStringLiteral("Text1")) {
                dialog->field_list()->setCurrentRow(row);
            }
        }
        accept_input_dialog(&run.input_seen, 5);
        dialog->formula_button()->click();
        dialog->accept();
    });
    QAction* action = gate::find_action(window, app::MenuAction::custom_fields);
    if (action != nullptr && action->isEnabled()) {
        action->trigger();
        QApplication::processEvents();
    }
    return run;
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
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: qt_sort_persist_tests <fixture.mpp> <out-dir> <project-result.mpp> "
                     "<control-null-save.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    const std::string project_result = argv[3];
    const std::string null_save_path = argv[4];

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "nothing is queued on open");

    // Name descending is the measured pair: it is queued, with Project's order.
    window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                             "Name descending", false);
    QApplication::processEvents();
    const app::StructuralEdit* name_desc = queued_sort(window);
    gate::check(name_desc != nullptr, "Name descending queues one sort_view edit");
    if (name_desc != nullptr) {
        gate::check(!name_desc->ascending, "the queued edit is descending");
        gate::check(name_desc->sort_order == std::vector<std::uint32_t>({5, 4, 1, 3, 2}),
                    "the queued order is the order Project saved");
    }
    gate::check(window.sort_active(), "the session sort is applied as well");
    gate::check(status_contains(window, "queued; File > Save writes it"),
                "the status line says the sort is queued");

    // A second sort replaces the queued one (Sort is exclusive).
    window.apply_sort_choice(one_key_sort("Name", true, "Name ascending"),
                             "Name ascending", false);
    QApplication::processEvents();
    const app::StructuralEdit* name_asc = queued_sort(window);
    gate::check(name_asc != nullptr, "Name ascending replaces the queued sort");
    if (name_asc != nullptr) {
        gate::check(name_asc->ascending, "the replacement is ascending");
    }

    // Clearing the sort withdraws the queued edit.
    window.apply_sort_choice(core::view::SortDefinition{}, "", true);
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "clearing the sort withdraws the queued edit");
    gate::check(!window.sort_active(), "clearing the sort clears the session sort");

    // W35g-0.1 (R35fG1-1): Project > Custom Fields... opens beside a queued Sort (looking at the
    // stored formulas queues nothing); only an accepted Formula... is refused, with the Sort
    // still queued and no formula queued.
    {
        window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                 "Name descending", false);
        QApplication::processEvents();
        gate::check(queued_sort(window) != nullptr, "Name descending is queued for Custom Fields");

        const CustomFieldsRun looked = run_custom_fields(window, false);
        gate::check(looked.dialog_seen, "Custom Fields opens beside a queued sort");
        gate::check(queued_sort(window) != nullptr,
                    "cancelling Custom Fields leaves the sort queued");
        gate::check(status_contains(window, "custom fields cancelled"),
                    "cancelling Custom Fields beside a queued sort: the status line says so");

        const CustomFieldsRun edited = run_custom_fields(window, true);
        gate::check(edited.dialog_seen, "Custom Fields opens again beside a queued sort");
        gate::check(!edited.input_seen,
                    "Formula... beside a queued sort opens no input dialog");
        gate::check(queued_sort(window) != nullptr,
                    "a refused formula leaves the sort queued, and queues nothing beside it");
        gate::check(status_contains(window,
                                    "a Sort is queued and File > Save would write it, not a "
                                    "custom-field formula edit"),
                    "Formula... beside a queued sort: the status line names the refusal");
        window.apply_sort_choice(core::view::SortDefinition{}, "", true);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty() && !window.document_dirty(),
                    "no custom-field formula was queued beside the sort");
    }

    // W35f-1.8 (R35eW-1): Sort is exclusive in the save, which refuses it beside a table / date
    // format edit.  The shell refuses at the change, in both orders.
    {
        window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                 "Name descending", false);
        QApplication::processEvents();
        gate::check(queued_sort(window) != nullptr, "Name descending is queued for the format case");
        gate::check(!queue_date_format(window, 5),
                    "Table / Date Format does not open beside a queued sort");
        gate::check(queued_sort(window) != nullptr, "the queued sort stays queued");
        gate::check(status_contains(window,
                                    "a Sort is queued and File > Save would write it, not a "
                                    "table / date format edit"),
                    "Table / Date Format beside a queued sort: the status line names it");
        window.apply_sort_choice(core::view::SortDefinition{}, "", true);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "the format case: the sort is withdrawn");

        // The other order, in a window of its own (a pending format edit cannot be discarded).
        msqt::MainWindow other;
        other.resize(msqt::kWindowWidth, msqt::kWindowHeight);
        QString other_error;
        gate::check(other.open_file(fixture, &other_error), "the fixture loads for the format-first case");
        other.show();
        QApplication::processEvents();
        gate::check(queue_date_format(other, 5), "the date format is queued first");
        other.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                "Name descending", false);
        QApplication::processEvents();
        gate::check(other.pending_edits().empty(), "a sort beside a pending date format queues nothing");
        gate::check(status_contains(other,
                                    "session only; the file keeps its order (a table / date "
                                    "format or custom-field formula edit is pending"),
                    "a sort beside a pending date format: the status line names it");
        gate::check(other.sort_active(), "the sort still applies to the session");
    }

    // W35c-1.4 (R35bW-2): a Sort the user replaced or cleared is withdrawn for good.  Left on
    // the redo list, Ctrl+Y re-queued it under whatever sort the display had by then, and
    // File > Save wrote a sort the rows no longer showed.
    {
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(redo != nullptr, "Redo is a menu action");
        if (redo != nullptr) {
            gate::check(!redo->isEnabled(), "clearing a queued sort leaves nothing to redo");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(), "Redo after a cleared sort queues nothing");
            gate::check(!window.sort_active(), "Redo after a cleared sort leaves the rows unsorted");

            // Name descending, then Start: the builder refuses Start, so the display sorts by
            // Start and the queued Name descending is withdrawn.
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            QApplication::processEvents();
            gate::check(queued_sort(window) != nullptr, "Name descending is queued for the redo case");
            window.apply_sort_choice(one_key_sort("Start", false, "Start descending"),
                                     "Start descending", false);
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "a refused replacement sort withdraws the queued one");
            gate::check(!redo->isEnabled(),
                        "a refused replacement sort leaves nothing to redo");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Redo after a refused replacement sort queues nothing");
            gate::check(window.sort_active(),
                        "Redo after a refused replacement sort leaves the session sort as it was");

            // A replacement the builder accepts queues the new sort, with no stale redo.
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            window.apply_sort_choice(one_key_sort("Name", true, "Name ascending"),
                                     "Name ascending", false);
            QApplication::processEvents();
            gate::check(!redo->isEnabled(), "a replacement sort leaves nothing to redo");
        }
        window.apply_sort_choice(core::view::SortDefinition{}, "", true);
        QApplication::processEvents();
    }

    // W35b-4.R7 (RW35-R7): Undo withdraws a queued sort from the file but not the
    // session sort (the rebuild keeps it), so the status line says the rows stay
    // sorted and the file keeps its order.
    window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                             "Name descending", false);
    QApplication::processEvents();
    gate::check(queued_sort(window) != nullptr, "Name descending is queued for the Undo case");
    if (QAction* undo = gate::find_action(window, app::MenuAction::undo)) {
        undo->trigger();
        QApplication::processEvents();
    } else {
        gate::check(false, "Undo is a menu action");
    }
    gate::check(window.pending_edits().empty(), "Undo withdraws the queued sort");
    gate::check(window.sort_active(), "Undo of a queued sort leaves the session sort applied");
    gate::check(status_contains(window, "undo the queued Sort; the rows stay in the sorted "
                                        "order for this session and File > Save will keep the "
                                        "file's order"),
                "Undo of a queued sort: the status line says the display keeps the sort");

    // W35f-0.3 (R35eG1-3): the five-task route of bypass (a).  Undo of a queued Sort leaves the
    // session sort shown with nothing queued and the Sort on the redo list; Level 1 is a measured
    // shape here, so before W35e-1.1(a) it queued under the sorted rows (and its push cleared the
    // redo list).  The queue is refused while the sort is shown (the Level is shown for the
    // session), and Ctrl+Y then re-queues the Sort the display shows.  The needles are the queue,
    // the status text and the displayed rows.
    {
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        msqt::SheetTableModel* model = window.sheet_table_model();
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(redo != nullptr && redo->isEnabled() && name_column >= 0,
                    "Undo then Level: Redo holds the withdrawn Sort");
        if (redo != nullptr && name_column >= 0) {
            const std::vector<QString> sorted_rows = displayed_names(model, name_column);
            window.apply_outline_level(1);
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Undo then Level: Level 1 under the shown Sort queues nothing");
            gate::check(status_contains(window,
                                        "session only; a session Sort is shown and File > Save "
                                        "would write the file's own order, so the level is not "
                                        "queued"),
                        "Undo then Level: the status line names the shown Sort and the refused "
                        "queue");
            // The displayed rows are the sorted rows, in the sorted order (a Level only hides).
            const std::vector<QString> level_rows = displayed_names(model, name_column);
            std::size_t next = 0;
            bool in_sorted_order = true;
            for (const QString& name : level_rows) {
                while (next < sorted_rows.size() && sorted_rows[next] != name) {
                    ++next;
                }
                if (next == sorted_rows.size()) {
                    in_sorted_order = false;
                    break;
                }
                ++next;
            }
            gate::check(in_sorted_order,
                        "Undo then Level: the displayed rows keep the sorted order");
            redo->trigger();
            QApplication::processEvents();
            gate::check(queued_sort(window) != nullptr,
                        "Undo then Level: Ctrl+Y re-queues the Sort the display shows");
            gate::check(displayed_names(model, name_column) == sorted_rows,
                        "Undo then Level: after Ctrl+Y every row is shown in the sorted order");
        }
    }
    window.apply_sort_choice(core::view::SortDefinition{}, "", true);
    QApplication::processEvents();
    gate::check(!window.sort_active(), "the Undo case leaves no session sort");

    // W35d-0.4 (R35cG1-1): the Undo-FIRST route.  Undo withdraws a queued Sort onto the redo
    // list, so a later refused or cleared Sort found nothing queued and kept that list, and
    // Ctrl+Y re-queued the withdrawn Sort while the rows showed another order (File > Save
    // then wrote Name descending).  Any sort choice made while a Sort sits on the redo list
    // ends it.  The needles are the redo state and the displayed order, not a status text.
    {
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        msqt::SheetTableModel* model = window.sheet_table_model();
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(undo != nullptr && redo != nullptr, "Undo and Redo are menu actions");
        if (undo != nullptr && redo != nullptr && name_column >= 0) {
            const std::vector<QString> file_order = displayed_names(model, name_column);

            // Name descending, Ctrl+Z, a Sort the builder refuses (Start), Ctrl+Y.
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            QApplication::processEvents();
            gate::check(queued_sort(window) != nullptr, "Undo-first: Name descending is queued");
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        "Undo-first: Undo withdraws the Sort onto the redo list");
            window.apply_sort_choice(one_key_sort("Start", false, "Start descending"),
                                     "Start descending", false);
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Undo-first: the refused Start sort queues nothing");
            const std::vector<QString> start_order = displayed_names(model, name_column);
            gate::check(!redo->isEnabled(),
                        "Undo-first: a refused Sort ends the redo list (Redo is disabled)");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Undo-first: Redo after a refused Sort queues nothing");
            gate::check(displayed_names(model, name_column) == start_order,
                        "Undo-first: Redo after a refused Sort leaves the rows as the Start sort "
                        "shows them");
            gate::check(window.sort_active(),
                        "Undo-first: the Start session sort is still the displayed sort");

            // Name descending, Ctrl+Z, clear the sort, Ctrl+Y.
            window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            QApplication::processEvents();
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            QApplication::processEvents();
            gate::check(queued_sort(window) != nullptr, "Undo-first: Name descending is queued again");
            undo->trigger();
            QApplication::processEvents();
            gate::check(redo->isEnabled(), "Undo-first: the second Undo fills the redo list");
            window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            QApplication::processEvents();
            gate::check(!window.sort_active() && displayed_names(model, name_column) == file_order,
                        "Undo-first: the cleared sort shows the file's order");
            gate::check(!redo->isEnabled(),
                        "Undo-first: a cleared Sort ends the redo list (Redo is disabled)");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Undo-first: Redo after a cleared Sort queues nothing");
            gate::check(!window.sort_active() && displayed_names(model, name_column) == file_order,
                        "Undo-first: Redo after a cleared Sort leaves the rows in the file's order");

            // The session-sort entry point the gates and the header use ends it too.
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            QApplication::processEvents();
            undo->trigger();
            QApplication::processEvents();
            window.clear_session_sort();
            QApplication::processEvents();
            gate::check(!redo->isEnabled(),
                        "Undo-first: clearing the session sort ends the redo list");

            // W35e-2.2 (R35dG0-4): apply_session_sort called DIRECTLY after an Undo.  Every route
            // above reaches its own clear through apply_sort_choice, which has already ended the
            // redo list; this one is the entry point the header and the gates use, and its own
            // clear is the only guard on it.
            window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            QApplication::processEvents();
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            QApplication::processEvents();
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        "Undo-first: Undo withdraws the Sort onto the redo list (direct session "
                        "sort)");
            window.apply_session_sort(one_key_sort("Name", true, "Name ascending"),
                                      "Name ascending");
            QApplication::processEvents();
            gate::check(!redo->isEnabled(),
                        "Undo-first: apply_session_sort ends the redo list (Redo is disabled)");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "Undo-first: Redo after apply_session_sort queues nothing");
            gate::check(window.sort_active(),
                        "Undo-first: the Name ascending session sort is still the displayed sort");

            // The plain route still works: Undo then Redo with nothing in between re-queues the
            // Sort.  W35e-2.2 (R35dG0-4): the rows are NOT asserted here.  Undo keeps the session
            // sort, so the rows already show the Sort before the re-apply in the rebuild runs
            // and a check on them passes with or without it; the re-apply has no route that
            // reaches it with a different display (every sort path above ends the redo list
            // first).  Its key mapping is tested in app_edits_sort (W35e-2.1).
            window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            QApplication::processEvents();
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            QApplication::processEvents();
            undo->trigger();
            QApplication::processEvents();
            redo->trigger();
            QApplication::processEvents();
            gate::check(queued_sort(window) != nullptr,
                        "Undo then Redo with nothing between re-queues the Sort");
            window.apply_sort_choice(core::view::SortDefinition{}, "", true);
            QApplication::processEvents();
        }
    }

    // Unmeasured shapes stay session only, each with its own reason.
    struct Refused {
        const char* field;
        const char* needle;
    };
    const Refused refused[] = {
        {"ID", "Sort by ID is not implemented in this port"},
        {"Start", "Sort by Start is not implemented in this port"},
        {"Finish", "Sort by Finish is not implemented in this port"},
        {"Priority", "Sort by this key is not implemented in this port"},
    };
    for (const Refused& row : refused) {
        const std::string label = std::string(row.field) + " descending";
        window.apply_sort_choice(one_key_sort(row.field, false, label.c_str()), label, false);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(),
                    (label + ": nothing is queued").c_str());
        gate::check(window.sort_active(), (label + ": the session sort is applied").c_str());
        gate::check(status_contains(window, row.needle),
                    (label + ": the status line names the reason").c_str());
        gate::check(status_contains(window, "session only"),
                    (label + ": the status line says session only").c_str());
    }
    {
        core::view::SortDefinition two = one_key_sort("Name", false, "Name, ID");
        two.keys.push_back(core::view::SortKey{app::task_field_id("ID"), true});
        window.apply_sort_choice(std::move(two), "Name, ID", false);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "two keys: nothing is queued");
        gate::check(status_contains(window, "Sort with 2 keys is not implemented in this port"),
                    "two keys: the status line names the reason");
    }

    // W35c-1.3 (R35bW-1): the other order of the same rule.  A grid edit made AFTER a Sort is
    // queued is refused with the queued edit named, so the screen cannot re-sort on names the
    // file does not store while File > Save writes the file's own order.
    {
        gate::check(window.open_file(fixture, &error), "the fixture reopens for the later-edit gate");
        QApplication::processEvents();
        window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                 "Name descending", false);
        QApplication::processEvents();
        gate::check(queued_sort(window) != nullptr, "Name descending is queued for the later-edit gate");
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        const int duration_column = msqt::entry_column_index(msqt::EntryCellKind::duration);
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(name_column >= 0 && duration_column >= 0, "the Entry table has Name and Duration");
        const QString name_before = model->data(model->index(1, name_column)).toString();
        gate::check(!model->setData(model->index(1, name_column),
                                    QStringLiteral("Renamed after the sort"), Qt::EditRole),
                    "a rename after a queued Sort is refused");
        gate::check(model->data(model->index(1, name_column)).toString() == name_before,
                    "the refused rename leaves the name as it was");
        gate::check(queued_sort(window) != nullptr, "the refused rename leaves the Sort queued");
        gate::check(status_contains(window, "a Sort is queued and File > Save would write it, not "
                                            "an edited cell"),
                    "a rename after a queued Sort: the status line names the queued Sort");
        gate::check(!model->setData(model->index(1, duration_column), QStringLiteral("9d"),
                                    Qt::EditRole),
                    "a Duration edit after a queued Sort is refused");
        // The task form and Task Information commit through apply_task_information.
        window.sheet()->select_cell(1, 0);
        msqt::TaskInformationValues values;
        gate::check(window.task_information_values(&values), "a task is selected for the form");
        values.name = "Renamed in the form";
        gate::check(!window.apply_task_information(values),
                    "a task form edit after a queued Sort is refused");
        gate::check(status_contains(window, "not an edited task field"),
                    "a task form edit after a queued Sort: the status line names the Sort");
        gate::check(queued_sort(window) != nullptr, "the refused form edit leaves the Sort queued");
        // Cut writes cells too.
        window.sheet()->select_cell(1, 0);
        QApplication::processEvents();
        if (QAction* cut = gate::find_action(window, app::MenuAction::cut)) {
            gate::check(cut->isEnabled(), "Cut is enabled on the selected cell");
            cut->trigger();
            QApplication::processEvents();
            gate::check(status_contains(window, "not a cut cell"),
                        "a cut after a queued Sort: the status line names the queued Sort");
        } else {
            gate::check(false, "Cut is a menu action");
        }
        gate::check(queued_sort(window) != nullptr, "the refused cut leaves the Sort queued");
        // W35d-1.1 (R35cG1-3): Paste writes the RecordSet through SheetModel::paste, not
        // setData, so the cell gate does not cover it; refuse_view_change("a pasted cell") is
        // its only guard.  Copy fills the window's own clipboard (it changes nothing), then
        // Paste onto another Name cell under the queued Sort.
        QAction* copy = gate::find_action(window, app::MenuAction::copy);
        QAction* paste = gate::find_action(window, app::MenuAction::paste);
        gate::check(copy != nullptr && paste != nullptr, "Copy and Paste are menu actions");
        if (copy != nullptr && paste != nullptr) {
            window.sheet()->select_cell(2, static_cast<std::size_t>(name_column));
            window.refresh_menu_state();
            QApplication::processEvents();
            copy->trigger();
            QApplication::processEvents();
            window.sheet()->select_cell(1, static_cast<std::size_t>(name_column));
            window.refresh_menu_state();
            QApplication::processEvents();
            const QString pasted_before = model->data(model->index(1, name_column)).toString();
            gate::check(paste->isEnabled(), "Paste is enabled on the selected cell");
            paste->trigger();
            QApplication::processEvents();
            gate::check(status_contains(window, "a Sort is queued and File > Save would write it, "
                                                "not a pasted cell"),
                        "a paste after a queued Sort: the status line names the queued Sort");
            gate::check(model->data(model->index(1, name_column)).toString() == pasted_before,
                        "the refused paste leaves the name as it was");
            gate::check(queued_sort(window) != nullptr, "the refused paste leaves the Sort queued");
        }
    }

    // W35d-1.2 (R35cG1-4): one rule for the queued-view guard (spec/writers/view-state.md).  While
    // a Sort is queued Save writes it over the file's own outline, so every other change to the
    // displayed outline is refused with the queued Sort named: Collapse All, the row toggle, the
    // Project Summary Task toggle, and a session Show Level or All Subtasks.  Expand All shares
    // the guard but is unreachable under a Sort: every rebuild (queueing, Redo) shows every row
    // expanded and the collapsing commands are refused, so Expand All stays disabled.
    {
        gate::check(window.open_file(fixture, &error), "the fixture reopens for the outline gate");
        QApplication::processEvents();
        const std::size_t full_rows = window.sheet_row_count();
        // Collapse a summary by hand while nothing is queued: the Sort's rebuild expands it again.
        for (const int row : {0, 1, 2}) {
            window.toggle_outline_row(row);
            QApplication::processEvents();
            if (window.sheet_row_count() < full_rows) {
                break;
            }
        }
        gate::check(window.sheet_row_count() < full_rows,
                    "a hand toggle with nothing queued collapses a summary");
        window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                 "Name descending", false);
        QApplication::processEvents();
        gate::check(queued_sort(window) != nullptr, "Name descending is queued for the outline gate");
        const std::size_t sorted_rows = window.sheet_row_count();
        const auto unchanged = [&](const char* label, const char* needle) {
            gate::check(queued_sort(window) != nullptr,
                        (std::string(label) + ": the Sort stays queued").c_str());
            gate::check(window.sheet_row_count() == sorted_rows,
                        (std::string(label) + ": no row changes").c_str());
            gate::check(status_contains(window, needle),
                        (std::string(label) + ": the status line names the queued Sort").c_str());
        };
        gate::check(sorted_rows == full_rows, "queueing the Sort shows every row expanded again");
        if (QAction* expand = gate::find_action(window, app::MenuAction::expand_all)) {
            gate::check(!expand->isEnabled(), "Expand All is disabled under a queued Sort");
        } else {
            gate::check(false, "Expand All is a menu action");
        }
        if (QAction* collapse = gate::find_action(window, app::MenuAction::collapse_all)) {
            gate::check(collapse->isEnabled(), "Collapse All is enabled while a summary is expanded");
            collapse->trigger();
            QApplication::processEvents();
            unchanged("Collapse All", "a Sort is queued and File > Save would write it, not this "
                                      "collapsed state");
        } else {
            gate::check(false, "Collapse All is a menu action");
        }
        for (int row = 0; row < static_cast<int>(sorted_rows); ++row) {
            window.toggle_outline_row(row);
            QApplication::processEvents();
        }
        unchanged("the row toggle", "a Sort is queued and File > Save would write it, not this "
                                    "expanded or collapsed row");
        if (QAbstractButton* summary = project_summary_box(window)) {
            const bool shown_before = window.project_summary_visible();
            summary->click();
            QApplication::processEvents();
            unchanged("the summary toggle", "a Sort is queued and File > Save would write it, not "
                                            "the project summary row shown or hidden");
            gate::check(window.project_summary_visible() == shown_before,
                        "the summary toggle leaves the summary as it was");
            gate::check(summary->isChecked() == shown_before,
                        "the summary toggle puts the check box back");
        } else {
            gate::check(false, "the Project Summary Task check box is in the page");
        }
        window.apply_outline_level(1);
        QApplication::processEvents();
        unchanged("a session Show Level", "a Sort is queued and File > Save would write it, not "
                                          "a session Show Level");
        window.apply_outline_level(0);
        QApplication::processEvents();
        unchanged("All Subtasks", "a Sort is queued and File > Save would write it, not all "
                                  "subtasks shown");
        // Clearing the Sort is not refused: it withdraws the queued Sort, and the outline
        // commands work again.
        window.apply_sort_choice(core::view::SortDefinition{}, "", true);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "clearing the Sort withdraws it");
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(!status_contains(window, "a Sort is queued"),
                    "with the Sort cleared a session Show Level is not refused");
    }

    // W35d-0.5 (R35cG1-2): Cut and Paste are cell edits and end the structural redo list, as
    // after_cell_commit() does.  Route: Sort Name descending, Ctrl+Z (nothing is queued, so the
    // cell gate is open), a cut or pasted Name, Ctrl+Y.  Left on the list, Redo re-queued the
    // Sort over the unsaved name without push() or the builder, and Save wrote the file's own.
    {
        gate::check(window.open_file(fixture, &error), "the fixture reopens for the cut and paste gate");
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
                window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                         "Name descending", false);
                QApplication::processEvents();
                gate::check(queued_sort(window) != nullptr,
                            (route + ": Name descending is queued").c_str());
                undo->trigger();
                QApplication::processEvents();
                gate::check(window.pending_edits().empty() && redo->isEnabled(),
                            (route + ": Undo withdraws the Sort onto the redo list").c_str());
                const int name_col = static_cast<int>(name_column);
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
                const std::vector<QString> edited = displayed_names(model, name_col);
                gate::check(!redo->isEnabled(),
                            (route + " ends the structural redo list (Redo is disabled)").c_str());
                redo->trigger();
                QApplication::processEvents();
                gate::check(window.pending_edits().empty(),
                            (route + ": Redo re-queues no Sort over the edited name").c_str());
                gate::check(displayed_names(model, name_col) == edited,
                            (route + ": Redo leaves the edited names as they were").c_str());
                window.apply_sort_choice(core::view::SortDefinition{}, "", true);
                QApplication::processEvents();
                gate::check(window.open_file(fixture, &error),
                            (route + ": the fixture reopens").c_str());
                QApplication::processEvents();
                model = window.sheet_table_model();
            }
        }
    }

    // W35e-0.1 (R35dG0-1): a PARTLY applied Task Information or Task Form edit.  Route: Sort Name
    // descending, Ctrl+Z (nothing is queued, so the field edit is accepted), a Name that commits
    // and a Duration that does not parse, Ctrl+Y.  The Name stays committed (no rollback), so the
    // apply ended with a cell edit and must end the structural redo list too; left on it, Redo
    // re-queued the Sort over the unsaved name without push() or the builder.
    {
        gate::check(window.open_file(fixture, &error), "the fixture reopens for the partial apply gate");
        QApplication::processEvents();
        const int name_col = msqt::entry_column_index(msqt::EntryCellKind::name);
        msqt::SheetTableModel* model = window.sheet_table_model();
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        gate::check(undo != nullptr && redo != nullptr && name_col >= 0,
                    "Undo, Redo and a Name column exist (partial apply)");
        if (undo != nullptr && redo != nullptr && name_col >= 0) {
            window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                     "Name descending", false);
            QApplication::processEvents();
            gate::check(queued_sort(window) != nullptr, "partial apply: Name descending is queued");
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        "partial apply: Undo withdraws the Sort onto the redo list");
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
            const std::vector<QString> edited = displayed_names(model, name_col);
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "partial apply: Redo re-queues no Sort over the edited name");
            gate::check(displayed_names(model, name_col) == edited,
                        "partial apply: Redo leaves the edited names as they were");
        }
    }

    // W35c-1.3: undo in the sheet, then Sort, then Ctrl+Y.  Queueing the Sort rebuilds the
    // document, and the rebuild's new sheet carries no history, so there is no sheet Redo to
    // re-apply the rename under the queued Sort.  This pins that: Redo is disabled and the
    // rename does not come back.
    {
        gate::check(window.open_file(fixture, &error), "the fixture reopens for the redo gate");
        QApplication::processEvents();
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(model->setData(model->index(1, name_column),
                                   QStringLiteral("Renamed then undone"), Qt::EditRole),
                    "a rename commits with nothing queued");
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        gate::check(undo != nullptr, "Undo is a menu action");
        if (undo != nullptr) {
            undo->trigger();
            QApplication::processEvents();
        }
        gate::check(!any_row_named(model, name_column, QStringLiteral("Renamed then undone")),
                    "the sheet's Undo takes the rename back");
        window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                 "Name descending", false);
        QApplication::processEvents();
        gate::check(queued_sort(window) != nullptr,
                    "Name descending queues after the sheet's Undo (the plan is clean)");
        if (QAction* redo = gate::find_action(window, app::MenuAction::redo)) {
            gate::check(!redo->isEnabled(),
                        "the queued Sort's rebuild leaves no sheet Redo");
            redo->trigger();
            QApplication::processEvents();
        } else {
            gate::check(false, "Redo is a menu action");
        }
        gate::check(!any_row_named(model, name_column, QStringLiteral("Renamed then undone")),
                    "Redo does not re-apply the rename under the queued Sort");
        gate::check(queued_sort(window) != nullptr, "Redo leaves the Sort queued");
    }

    // W35b-1.5 (RW35-R2): a grid edit that is not saved yet.  The session plan
    // carries the rename, so Sort would persist an order of names the file does
    // not store; the builder refuses it, nothing is queued, and the status line
    // says to undo the grid edits first (or save them).
    {
        gate::check(window.open_file(fixture, &error),
                    "the fixture reopens for the unsaved-edit gate");
        QApplication::processEvents();
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        gate::check(name_column >= 0, "the Entry table has a Name column");
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(name_column >= 0 &&
                        model->setData(model->index(1, name_column),
                                       QStringLiteral("Renamed in the grid"), Qt::EditRole),
                    "a Name cell edit commits through the shell's model path");
        window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                 "Name descending", false);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "unsaved rename: the sort queues nothing");
        gate::check(status_contains(window, "Sort is not implemented in this port while the "
                                            "plan carries unsaved edits"),
                    "unsaved rename: the status line names the unsaved edits");
        gate::check(status_contains(window, "(or save them, where Save writes that field)"),
                    "unsaved rename: the status line says undo the grid edits first (or save them)");
        gate::check(status_contains(window, "session only"),
                    "unsaved rename: the status line says session only");
    }

    // W35b-3.4: Name descending on the control, saved through File > Save, is Project's own
    // save of the pair (sort-name-desc), every stream under the null-save discipline.
    {
        std::error_code file_error;
        std::filesystem::create_directories(out_dir.toStdString(), file_error);
        const QString saved_path = out_dir + QStringLiteral("/w35b-qt-sort-name-desc-saved.mpp");
        std::filesystem::remove(saved_path.toStdString(), file_error);
        gate::check(window.open_file(fixture, &error), "the fixture reopens for the save");
        QApplication::processEvents();
        window.apply_sort_choice(one_key_sort("Name", false, "Name descending"),
                                 "Name descending", false);
        QApplication::processEvents();
        gate::check(queued_sort(window) != nullptr, "Name descending is queued for the save");
        QString save_error;
        const bool saved = window.save_document_path(saved_path, &save_error);
        gate::check(saved, "the queued sort saves");
        if (!saved) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
        } else {
            try {
                fixture::require_committed(project_result);
                fixture::require_committed(null_save_path);
                const mppwrite::CfbDocument control_doc =
                    mppwrite::CfbDocument::from_file(fixture.toStdString());
                const mppwrite::CfbDocument null_doc =
                    mppwrite::CfbDocument::from_file(null_save_path);
                const mppwrite::CfbDocument project_doc =
                    mppwrite::CfbDocument::from_file(project_result);
                const mppwrite::CfbDocument saved_doc =
                    mppwrite::CfbDocument::from_file(saved_path.toStdString());
                PairContext context;
                const std::array<const char*, 3> declared{
                    {"214/CV_iew/Var2Data", "Props14", "214/CEdl/Var2Data"}};
                save_pair::compare_all(context, "qt sort name-desc", control_doc, null_doc,
                                       project_doc, saved_doc, nullptr, 0, declared.data(),
                                       declared.size());
            } catch (const std::exception& pair_error) {
                std::fprintf(stderr, "note: pair comparison error: %s\n", pair_error.what());
                gate::check(false, "the saved file is compared with Project's pair");
            }
        }
    }

    std::printf("qt_sort_persist: ");
    return gate::finish();
}
