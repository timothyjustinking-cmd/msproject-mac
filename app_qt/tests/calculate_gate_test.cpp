// app_qt/tests/calculate_gate_test.cpp -- wave 35 (W35-1.3).
//
// THE RULE THIS GATES
//   Project > Schedule > Calculate Project is enabled for exactly the measured
//   shape: the project row plus two unlinked, automatically scheduled leaves
//   whose recalculation is PENDING, alone in the save
//   (tests/data/wave35-calculate-unlinked-control.mpp ->
//   wave35-calculate-unlinked.mpp, Project's own saves; the field-by-field pair
//   is app_edits_calculate).  Every other project is refused up front with the
//   decision-9 reason in the status line and queues nothing.
//
// WHAT IT PROVES, in the real MainWindow
//   * the Calculate Project button is live and carries a tooltip other than the
//     disabled one;
//   * on a linked project (wave35-calculate-linked-control.mpp) it queues
//     nothing, leaves the document clean and names the refusal;
//   * on the measured control it queues exactly one edit and dirties the
//     document; a second click while it is pending is refused (queue stays 1);
//   * the real save writes it (the file differs from the control) and a reopen
//     of the saved file refuses a second calculation (nothing is pending).
//
//   * (W35b-2.6) the saved file equals Project's own save of the pair stream by stream under the
//     null-save discipline (tests/save_pair_compare.h), not merely "differs from the control".
//
// Usage: qt_calculate_gate <unlinked-control.mpp> <linked-control.mpp> <out-dir>
//                          <project-result.mpp> <control-null-save.mpp>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "dialogs.h"
#include "edit_model.h"
#include "entry_columns.h"
#include "fixture_guard.h"
#include "gate_util.h"
#include "main_window.h"
#include "project_dialogs_qt.h"
#include "ribbon_bar.h"
#include "save_pair_compare.h"
#include "sheet_table_model.h"
#include "shell_style.h"

namespace {

// The Calculate Project button of Project > Schedule.  The ribbon page is
// rebuilt by set_active_tab, so a pointer is never cached.
QAbstractButton* calculate_button(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int tab = ribbon->tab_titles().indexOf(QStringLiteral("Project"));
    if (tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(tab);
    QApplication::processEvents();
    const int group = ribbon->group_titles().indexOf(QStringLiteral("Schedule"));
    if (group < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(group, control));
        if (button == nullptr) {
            continue;
        }
        QString text = button->text().isEmpty() ? button->accessibleName() : button->text();
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (text.simplified() == QStringLiteral("Calculate Project")) {
            return button;
        }
    }
    return nullptr;
}

bool click_calculate(msqt::MainWindow& window) {
    QAbstractButton* button = calculate_button(window);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

// Fills and accepts the modal QInputDialog the Custom Fields dialog's Formula... button opens.
// It polls (the dialog is not the active modal widget until its event loop is running) and
// gives up after `tries` rounds, so a missing dialog cannot hang the gate.
void fill_input_dialog(const QString& text, int tries) {
    QTimer::singleShot(10, [text, tries]() {
        if (auto* input = dynamic_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            input->setTextValue(text);
            input->accept();
        } else if (tries > 0) {
            fill_input_dialog(text, tries - 1);
        }
    });
}

struct PairContext {
    void check(bool ok, const std::string& what) const { gate::check(ok, what.c_str()); }
    void expected_failure(bool unexpected_pass, const std::string& what) const {
        gate::check(false, ("unexpected KnownGap report: " + what +
                            (unexpected_pass ? " (gap gone)" : ""))
                               .c_str());
    }
};

std::vector<char> read_all(const QString& path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: qt_calculate_gate <unlinked-control.mpp> <linked-control.mpp> "
                     "<out-dir> <project-result.mpp> <control-null-save.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString other_fixture = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = QString::fromLocal8Bit(argv[3]);
    const std::string project_result = argv[4];
    const std::string null_save_path = argv[5];
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave35-calculate-unlinked-control.mpp is present");
    gate::check(std::filesystem::exists(other_fixture.toStdString()),
                "the committed linked control is present");
    if (!std::filesystem::exists(fixture.toStdString()) ||
        !std::filesystem::exists(other_fixture.toStdString())) {
        return gate::finish();
    }
    const QString copy = out_dir + QStringLiteral("/w35-calculate-control.mpp");
    const QString other_copy = out_dir + QStringLiteral("/w35-calculate-linked.mpp");
    const QString calculated = out_dir + QStringLiteral("/w35-calculate-saved.mpp");
    std::error_code file_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the control copy is written under the build tree");
    std::filesystem::copy_file(other_fixture.toStdString(), other_copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the linked copy is written under the build tree");
    std::filesystem::remove(calculated.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }

    // --- 1. the button, the refusal that queues nothing ----------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(other_copy, &error), "the linked project opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        {
            QAbstractButton* button = calculate_button(window);
            gate::check(button != nullptr && button->isEnabled(),
                        "Calculate Project is live (W35-1.3)");
            gate::check(button != nullptr &&
                            button->toolTip() != QStringLiteral("not implemented in this port"),
                        "the live button does not carry the disabled tooltip");
        }
        gate::check(click_calculate(window), "Calculate Project clicks on a linked project");
        gate::check(window.pending_edits().empty(), "a linked project queues nothing");
        gate::check(!window.document_dirty(), "the refusal leaves the document clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("Calculate Project is not implemented in this port")),
                    "the status line names the refusal");
        // W35b-2.5 (RW35-R5): the prefix is on every refusal; the plan-side link gate is the one
        // that must fire for a linked project.
        gate::check(window.status_left_text().contains(
                        QStringLiteral("for a project with links")),
                    "the status line names the link gate, not another refusal");
    }

    // --- 2. the measured control, the save -----------------------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        gate::check(click_calculate(window), "Calculate Project clicks on the control");
        gate::check(window.pending_edits().size() == 1, "Calculate Project queues one edit");
        gate::check(window.document_dirty(), "the queued calculation dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral("queued Calculate Project")),
                    "the status line reports the queued calculation");

        // A second click while it is pending: the edit is exclusive.
        gate::check(click_calculate(window), "a second Calculate Project clicks");
        gate::check(window.pending_edits().size() == 1, "a second click keeps the queue at one");

        // W35d-2.11 (R35cW-4): a grid edit made AFTER Calculate Project is queued is refused with
        // the queued Calculate named, the same guard as a queued Show Level or Sort; the queue
        // stays at the one Calculate edit and the document keeps its values.
        {
            const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
            const int duration_column = msqt::entry_column_index(msqt::EntryCellKind::duration);
            msqt::SheetTableModel* model = window.sheet_table_model();
            gate::check(model != nullptr && name_column >= 0 && duration_column >= 0,
                        "the Entry table has Name and Duration");
            if (model != nullptr && name_column >= 0 && duration_column >= 0) {
                const QString name_before = model->data(model->index(1, name_column)).toString();
                gate::check(!model->setData(model->index(1, name_column),
                                            QStringLiteral("Renamed after the calculation"),
                                            Qt::EditRole),
                            "a rename after a queued Calculate Project is refused");
                gate::check(model->data(model->index(1, name_column)).toString() == name_before,
                            "the refused rename leaves the name as it was");
                gate::check(window.status_left_text().contains(QStringLiteral(
                                "a Calculate Project is queued and File > Save would write it, "
                                "not an edited cell")),
                            "a rename after a queued Calculate Project: the status line names it");
                gate::check(!model->setData(model->index(1, duration_column),
                                            QStringLiteral("9d"), Qt::EditRole),
                            "a Duration edit after a queued Calculate Project is refused");
                gate::check(window.pending_edits().size() == 1,
                            "the refused edits leave the one queued Calculate Project");

                // W35e-2.5 (R35dG2-1): Task Information, Cut and Paste each have a guard of
                // their own (refuse_data_change at their entry), which the cell gate above does
                // not reach: Cut and Paste write the RecordSet directly and the form commits
                // through apply_task_information.  Each is refused with the Calculate named and
                // the queue stays at one.  Fill Down's guard is not tested here: the handler is
                // private and its ribbon and row-menu controls are disabled (W31-0.4), so no
                // route reaches it (qt_fill_gate pins the disabled control).
                window.sheet()->select_cell(1, 0);
                QApplication::processEvents();
                msqt::TaskInformationValues values;
                gate::check(window.task_information_values(&values),
                            "a task is selected for the form");
                values.name = "Renamed in the form after the calculation";
                gate::check(!window.apply_task_information(values),
                            "a task form edit after a queued Calculate Project is refused");
                gate::check(window.status_left_text().contains(QStringLiteral(
                                "a Calculate Project is queued and File > Save would write it, "
                                "not an edited task field")),
                            "a task form edit after a queued Calculate Project: the status line "
                            "names it");
                gate::check(window.pending_edits().size() == 1,
                            "the refused form edit leaves the one queued Calculate Project");

                if (QAction* cut = gate::find_action(window, app::MenuAction::cut)) {
                    gate::check(cut->isEnabled(), "Cut is enabled on the selected cell");
                    cut->trigger();
                    QApplication::processEvents();
                    gate::check(window.status_left_text().contains(QStringLiteral(
                                    "a Calculate Project is queued and File > Save would write "
                                    "it, not a cut cell")),
                                "a cut after a queued Calculate Project: the status line names "
                                "it");
                } else {
                    gate::check(false, "Cut is a menu action");
                }
                gate::check(window.pending_edits().size() == 1,
                            "the refused cut leaves the one queued Calculate Project");

                QAction* copy_action = gate::find_action(window, app::MenuAction::copy);
                QAction* paste = gate::find_action(window, app::MenuAction::paste);
                gate::check(copy_action != nullptr && paste != nullptr, "Copy and Paste are menu actions");
                if (copy_action != nullptr && paste != nullptr) {
                    window.sheet()->select_cell(2, static_cast<std::size_t>(name_column));
                    window.refresh_menu_state();
                    QApplication::processEvents();
                    copy_action->trigger();
                    QApplication::processEvents();
                    window.sheet()->select_cell(1, static_cast<std::size_t>(name_column));
                    window.refresh_menu_state();
                    QApplication::processEvents();
                    gate::check(paste->isEnabled(), "Paste is enabled on the selected cell");
                    paste->trigger();
                    QApplication::processEvents();
                    gate::check(window.status_left_text().contains(QStringLiteral(
                                    "a Calculate Project is queued and File > Save would write "
                                    "it, not a pasted cell")),
                                "a paste after a queued Calculate Project: the status line "
                                "names it");
                    gate::check(model->data(model->index(1, name_column)).toString() ==
                                    name_before,
                                "the refused paste leaves the name as it was");
                }
                gate::check(window.pending_edits().size() == 1,
                            "the refused paste leaves the one queued Calculate Project");
            }
        }

        QString save_error;
        const bool ok = window.save_document_path(calculated, &save_error);
        gate::check(ok, "the calculation saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(calculated) != read_all(copy),
                    "the saved file differs from the control");

        // W35b-2.6: "differs from the control" holds for any write, so the save is compared
        // with Project's own pair, every stream under the null-save discipline (as
        // app_edits_calculate does for the cascade).  A wrong leaf stamp, a missing record or a
        // stray byte fails here.
        try {
            fixture::require_committed(project_result);
            fixture::require_committed(null_save_path);
            const mppwrite::CfbDocument control_doc =
                mppwrite::CfbDocument::from_file(copy.toStdString());
            const mppwrite::CfbDocument null_doc =
                mppwrite::CfbDocument::from_file(null_save_path);
            const mppwrite::CfbDocument project_doc =
                mppwrite::CfbDocument::from_file(project_result);
            const mppwrite::CfbDocument saved_doc =
                mppwrite::CfbDocument::from_file(calculated.toStdString());
            PairContext context;
            const std::array<const char*, 8> declared{
                {"114/Props", "114/TBkndTask/FixedData", "114/TBkndTask/FixedMeta",
                 "114/TBkndTask/Fixed2Data", "114/TBkndTask/Var2Data", "114/TBkndTask/VarMeta",
                 "\005DocumentSummaryInformation", "Props14"}};
            save_pair::compare_all(context, "qt calculate", control_doc, null_doc, project_doc,
                                   saved_doc, nullptr, 0, declared.data(), declared.size());
        } catch (const std::exception& pair_error) {
            std::fprintf(stderr, "note: pair comparison error: %s\n", pair_error.what());
            gate::check(false, "the saved file is compared with Project's pair");
        }
    }

    // --- 2b. W35e-0.1 (R35dG0-1, R35dW-1): a partly applied Task Information edit ----------
    // Calculate Project, Ctrl+Z (nothing queued, the cell gate open), a Name that commits and a
    // Duration that does not parse, Ctrl+Y.  The Name stays committed, and left on the structural
    // redo list Redo re-queued the withdrawn Calculate over the changed task without push().
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control reopens for the partial apply");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(undo != nullptr && redo != nullptr && name_column >= 0 && model != nullptr,
                    "Undo, Redo and a Name column exist (partial apply)");
        if (undo != nullptr && redo != nullptr && name_column >= 0 && model != nullptr) {
            gate::check(click_calculate(window) && window.pending_edits().size() == 1,
                        "partial apply: Calculate Project is queued");
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        "partial apply: Undo withdraws Calculate Project onto the redo list");
            window.sheet()->select_cell(1, 0);
            msqt::TaskInformationValues values;
            gate::check(window.task_information_values(&values),
                        "partial apply: a task is selected for the form");
            values.name = "Renamed by a partial apply";
            values.duration_text = "not a duration";
            gate::check(!window.apply_task_information(values),
                        "partial apply: the form edit fails on the Duration");
            // W35f-1.3 (R35eG0-3): the status says what stuck and that Undo takes it back.
            gate::check(window.status_left_text().contains(
                            QStringLiteral("Name applied (Undo takes it back)")),
                        "partial apply: the status names the field that stuck and Undo");
            gate::check(model->data(model->index(1, name_column)).toString() ==
                            QStringLiteral("Renamed by a partial apply"),
                        "partial apply: the Name field stays committed");
            gate::check(!redo->isEnabled(),
                        "partial apply: the committed Name ends the structural redo list "
                        "(Redo is disabled)");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "partial apply: Redo re-queues no Calculate Project over the edited task");
            gate::check(model->data(model->index(1, name_column)).toString() ==
                            QStringLiteral("Renamed by a partial apply"),
                        "partial apply: Redo leaves the edited name as it was");

            // W35f-1.2 (R35eG0-2): the Task Form's entry and the structural-trial variant.  A
            // percent edit (structural), a Name that commits and a Duration that does not parse:
            // the trial must stay dropped (nothing queued), the Name stays committed, and the
            // status keeps naming the Duration failure (after_cell_change must not overwrite it).
            msqt::TaskInformationValues second;
            gate::check(window.task_information_values(&second),
                        "partial apply (trial): the values are readable");
            {
                second.percent_text = "40%";
                second.name = "Second partial apply";
                second.duration_text = "not a duration";
                gate::check(!window.apply_task_information(second, "task form"),
                            "partial apply (trial): the task form entry fails on the Duration");
                gate::check(window.pending_edits().empty(),
                            "partial apply (trial): the dropped trial queues no percent edit");
                gate::check(model->data(model->index(1, name_column)).toString() ==
                                QStringLiteral("Second partial apply"),
                            "partial apply (trial): the Name field stays committed");
                gate::check(window.status_left_text().contains(
                                QStringLiteral("task form: ")) &&
                                window.status_left_text().contains(QStringLiteral("duration"),
                                                                   Qt::CaseInsensitive),
                            "partial apply (trial): the status names the task form's Duration "
                            "failure");
                gate::check(!redo->isEnabled(),
                            "partial apply (trial): the redo list stays cleared");
            }
        }
    }

    // --- 2c. W35e-2.6 (R35dG2-2): the document-edit dialogs and Calculate Project ------------
    // The save refuses a Calculate Project beside a table / date format or a custom-field
    // formula edit.  The shell now says so at the moment of the change, in either order, before
    // the dialog opens.  A timer closes any modal dialog that does open (a missing refusal would
    // otherwise block exec() for good) and the gate fails on it.
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control reopens for the dialog gate");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        // Calculate first, then each dialog's menu action.
        gate::check(click_calculate(window) && window.pending_edits().size() == 1,
                    "dialog gate: Calculate Project is queued");
        struct Surface {
            app::MenuAction action;
            const char* needle;
            const char* label;
        };
        const Surface surfaces[] = {
            {app::MenuAction::table_date_format,
             "a Calculate Project is queued and File > Save would write it, not a table / date "
             "format edit",
             "Table / Date Format"},
            {app::MenuAction::custom_field_formula,
             "a Calculate Project is queued and File > Save would write it, not a custom-field "
             "formula edit",
             "Custom Field Formula"},
            // W35f-0.2 (R35eG2-1): Project > Custom Fields... reaches the same formula edit
            // through its Formula... button.
            {app::MenuAction::custom_fields,
             "a Calculate Project is queued and File > Save would write it, not a custom-field "
             "formula edit",
             "Custom Fields"},
        };
        for (const Surface& surface : surfaces) {
            const std::string label = surface.label;
            QAction* action = gate::find_action(window, surface.action);
            gate::check(action != nullptr && action->isEnabled(),
                        (label + " is an enabled menu action").c_str());
            if (action == nullptr || !action->isEnabled()) {
                continue;
            }
            bool dialog_opened = false;
            QTimer::singleShot(0, [&dialog_opened]() {
                if (QWidget* modal = QApplication::activeModalWidget()) {
                    dialog_opened = true;
                    modal->close();
                }
            });
            action->trigger();
            QApplication::processEvents();
            QApplication::processEvents();
            gate::check(!dialog_opened,
                        (label + " beside a queued Calculate Project opens no dialog").c_str());
            gate::check(window.status_left_text().contains(QString::fromLatin1(surface.needle)),
                        (label + " beside a queued Calculate Project: the status line names it")
                            .c_str());
            gate::check(window.pending_edits().size() == 1,
                        (label + ": the queue stays at the one Calculate Project").c_str());
        }
    }
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control reopens for the reverse order");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        // A date-format edit first, then Calculate Project.
        bool dialog_seen = false;
        QTimer::singleShot(0, [&dialog_seen]() {
            auto* dialog =
                dynamic_cast<msqt::TableDateFormatDialog*>(QApplication::activeModalWidget());
            if (dialog == nullptr) {
                return;
            }
            dialog_seen = true;
            const int index = dialog->date_format_combo()->findData(QVariant(4));
            if (index >= 0) {
                dialog->date_format_combo()->setCurrentIndex(index);
            }
            dialog->accept();
        });
        QAction* date_format = gate::find_action(window, app::MenuAction::table_date_format);
        gate::check(date_format != nullptr && date_format->isEnabled(),
                    "Table / Date Format is an enabled menu action (reverse order)");
        if (date_format != nullptr && date_format->isEnabled()) {
            date_format->trigger();
            QApplication::processEvents();
            gate::check(dialog_seen && window.document_dirty(),
                        "a table / date format edit is pending before the calculation");
            gate::check(click_calculate(window),
                        "Calculate Project clicks beside a pending date-format edit");
            gate::check(window.pending_edits().empty(),
                        "Calculate Project beside a pending date-format edit queues nothing");
            gate::check(window.status_left_text().contains(QStringLiteral(
                            "a table / date format or custom-field formula edit is pending")),
                        "Calculate Project beside a pending date-format edit: the status line "
                        "names it");
            // W35f-1.6 (R35eG2-4): the advice must not lead into the next refusal.  Saving a
            // custom-field formula leaves a file Calculate Project refuses, so the line says so
            // and offers the reopen.
            gate::check(window.status_left_text().contains(QStringLiteral(
                            "a file saved with a custom-field formula cannot take Calculate "
                            "Project) or reopen without saving")) &&
                            !window.status_left_text().contains(
                                QStringLiteral("save that edit first")),
                        "Calculate Project beside a pending edit: the advice names the formula "
                        "case and the reopen");
        }
    }

    // --- 2c'. W35f-1.7 (R35eG2-6): the formula edit first, then Calculate Project -------------
    // The reverse-order block above queues a date format only.  The guard is one predicate
    // (has_pending_edits), but the formula half of it was never reached by a test.
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control reopens for the formula order");
        window.show();
        QApplication::processEvents();
        bool dialog_seen = false;
        QTimer::singleShot(0, [&dialog_seen]() {
            auto* dialog =
                dynamic_cast<msqt::CustomFormulaDialog*>(QApplication::activeModalWidget());
            if (dialog == nullptr) {
                return;
            }
            dialog_seen = true;
            dialog->field_combo()->setCurrentIndex(0);
            dialog->formula_edit()->setText(QStringLiteral("[Duration]/240"));
            dialog->accept();
        });
        QAction* formula = gate::find_action(window, app::MenuAction::custom_field_formula);
        gate::check(formula != nullptr && formula->isEnabled(),
                    "Custom Field Formula is an enabled menu action (formula first)");
        if (formula != nullptr && formula->isEnabled()) {
            formula->trigger();
            QApplication::processEvents();
            gate::check(dialog_seen && window.document_dirty(),
                        "a custom-field formula edit is pending before the calculation");
            gate::check(click_calculate(window),
                        "Calculate Project clicks beside a pending formula edit");
            gate::check(window.pending_edits().empty(),
                        "Calculate Project beside a pending formula edit queues nothing");
            gate::check(window.status_left_text().contains(QStringLiteral(
                            "a table / date format or custom-field formula edit is pending")),
                        "Calculate Project beside a pending formula edit: the status line "
                        "names it");
        }
    }

    // --- 2d. W35f-1.5 (R35eG2-2): Undo, then a document edit, then Redo ----------------------
    // Calculate Project, Ctrl+Z (nothing queued, so each dialog accepts), then a table / date
    // format, a custom-field formula or a Custom Fields formula.  The document edit is pending,
    // so Ctrl+Y would re-queue the withdrawn Calculate beside it without push(): the shell ends
    // the structural redo list instead and Redo is disabled.
    {
        enum class Route { date_format, custom_formula, custom_fields };
        const struct {
            Route route;
            const char* label;
        } routes[] = {{Route::date_format, "Table / Date Format"},
                      {Route::custom_formula, "Custom Field Formula"},
                      {Route::custom_fields, "Custom Fields"}};
        for (const auto& item : routes) {
            const std::string label = item.label;
            msqt::MainWindow window;
            window.resize(msqt::kParityWidth, 800);
            QString error;
            gate::check(window.open_file(copy, &error),
                        (label + ": the control reopens for the redo case").c_str());
            window.show();
            QApplication::processEvents();
            QAction* undo = gate::find_action(window, app::MenuAction::undo);
            QAction* redo = gate::find_action(window, app::MenuAction::redo);
            if (undo == nullptr || redo == nullptr) {
                gate::check(false, (label + ": Undo and Redo exist").c_str());
                continue;
            }
            gate::check(click_calculate(window) && window.pending_edits().size() == 1,
                        (label + ": Calculate Project is queued").c_str());
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        (label + ": Undo withdraws Calculate Project onto the redo list").c_str());

            bool dialog_seen = false;
            QAction* action = nullptr;
            if (item.route == Route::date_format) {
                action = gate::find_action(window, app::MenuAction::table_date_format);
                QTimer::singleShot(0, [&dialog_seen]() {
                    auto* dialog = dynamic_cast<msqt::TableDateFormatDialog*>(
                        QApplication::activeModalWidget());
                    if (dialog == nullptr) {
                        return;
                    }
                    dialog_seen = true;
                    const int index = dialog->date_format_combo()->findData(QVariant(4));
                    if (index >= 0) {
                        dialog->date_format_combo()->setCurrentIndex(index);
                    }
                    dialog->accept();
                });
            } else if (item.route == Route::custom_formula) {
                action = gate::find_action(window, app::MenuAction::custom_field_formula);
                QTimer::singleShot(0, [&dialog_seen]() {
                    auto* dialog = dynamic_cast<msqt::CustomFormulaDialog*>(
                        QApplication::activeModalWidget());
                    if (dialog == nullptr) {
                        return;
                    }
                    dialog_seen = true;
                    dialog->field_combo()->setCurrentIndex(0);
                    dialog->formula_edit()->setText("[Duration]/240");
                    dialog->accept();
                });
            } else {
                action = gate::find_action(window, app::MenuAction::custom_fields);
                // The dialog's Formula... button opens a nested input dialog: a second timer
                // fills and accepts it while the first is inside click().
                QTimer::singleShot(0, [&dialog_seen]() {
                    auto* dialog =
                        dynamic_cast<msqt::CustomFieldsDialog*>(QApplication::activeModalWidget());
                    if (dialog == nullptr) {
                        return;
                    }
                    dialog_seen = true;
                    for (int row = 0; row < dialog->field_list()->count(); ++row) {
                        if (dialog->field_list()->item(row)->text() == QStringLiteral("Text1")) {
                            dialog->field_list()->setCurrentRow(row);
                        }
                    }
                    fill_input_dialog(QStringLiteral("[Duration]/240"), 200);
                    dialog->formula_button()->click();
                    dialog->accept();
                });
            }
            gate::check(action != nullptr && action->isEnabled(),
                        (label + " is an enabled menu action (redo case)").c_str());
            if (action == nullptr || !action->isEnabled()) {
                continue;
            }
            action->trigger();
            QApplication::processEvents();
            gate::check(dialog_seen && window.document_dirty(),
                        (label + ": the document edit is pending after Undo").c_str());
            gate::check(!redo->isEnabled(),
                        (label + ": the pending document edit ends the structural redo list "
                                 "(Redo is disabled)")
                            .c_str());
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        (label + ": Redo re-queues no Calculate Project beside the document edit")
                            .c_str());
        }
    }

    // --- 3. reopen: nothing is pending any more ------------------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(calculated, &error);
        gate::check(opened, "the calculated project reopens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        gate::check(click_calculate(window), "Calculate Project clicks on the reopened file");
        gate::check(window.pending_edits().empty(),
                    "a second calculation on the saved file queues nothing");
        gate::check(!window.document_dirty(), "the refused calculation leaves the file clean");
        gate::check(window.status_left_text().contains(QStringLiteral("not pending")),
                    "the status line names the refusal on the reopened file");
    }
    return gate::finish();
}
