// app_qt/tests/task_mode_document_edit_gate_test.cpp -- wave 35 (W35-0.2, RW34b-3), reworked in
// wave 35f (W35f-1.8, R35eW-1).
//
// THE RULE THIS GATES
//   A Manually Schedule edit and a pending table / date-format edit are two
//   different writers over one file; no Project pair measures them together, so
//   the shared save cascade refuses the combination (R34b-6,
//   app::save_structural_edits; covered at the app level for every exclusive kind).
//   W35f-1.8: the shell refuses the SECOND edit at the change, in both orders, with the
//   reason in the status line; the first stays queued and the second queues nothing.
//
// WHAT IT DRIVES (all through the shell's own controls)
//   * wave30-task-mode-auto-control.mpp: Project's own save of an automatically
//     scheduled task;
//   * Task > Tasks > Manually Schedule on task 1 (queues one edit);
//   * Table > Date Format on the first table (queues the document edit), in
//     both orders.
//
// Usage: qt_task_mode_document_edit_gate <wave30-task-mode-auto-control.mpp> <out-dir>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "dialogs.h"
#include "edit_model.h"
#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "project_dialogs_qt.h"
#include "ribbon_bar.h"
#include "sheet.h"
#include "sheet_table_model.h"
#include "shell_style.h"

namespace {

// The live Tasks-group Manually Schedule control.  The ribbon page is rebuilt by
// set_active_tab, so a pointer is never cached.
QAbstractButton* manual_button(msqt::MainWindow& window) {
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
    const int group = ribbon->group_titles().indexOf(QStringLiteral("Tasks"));
    if (group < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group); ++control) {
        if (ribbon->group_control_command(group, control) ==
            msqt::RibbonCommand::manually_schedule) {
            return qobject_cast<QAbstractButton*>(ribbon->group_control(group, control));
        }
    }
    return nullptr;
}

void select_task(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        if (sheet->row_uid(row) == uid) {
            const std::size_t column =
                window.sheet_table_model()->visible_column_for_field(app::name_field_id());
            sheet->select_cell(row, column == core::sheet::kNoIndex ? 0 : column);
            window.refresh_menu_state();
            QApplication::processEvents();
            return;
        }
    }
}

bool click_manual(msqt::MainWindow& window, std::uint32_t uid) {
    select_task(window, uid);
    QAbstractButton* button = manual_button(window);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    return true;
}

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

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_task_mode_document_edit_gate <task-mode-auto-control.mpp> "
                     "<out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave30-task-mode-auto-control.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }
    const QString copy = out_dir + QStringLiteral("/w35-task-mode-combo.mpp");
    const QString target = out_dir + QStringLiteral("/w35-task-mode-combo-saved.mpp");
    std::error_code file_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the control copy is written under the build tree");
    std::filesystem::remove(target.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }

    // Both orders.  W35f-1.8 (R35eW-1): the shell refuses the second edit at the change (the
    // save cascade's own refusal of the pair, R34b-6, stays covered by app_edits_task_mode and
    // the other exclusive kinds' app-level tests, which hand-build the queue).
    for (const bool format_first : {false, true}) {
        const char* const order = format_first ? " (date format first)" : " (Manually Schedule first)";
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        if (format_first) {
            gate::check(queue_date_format(window, 5), "the Table / Date Format dialog queues");
            gate::check(click_manual(window, 1), "Manually Schedule clicks on task 1");
            gate::check(window.pending_edits().empty(),
                        "Manually Schedule beside a pending date format queues nothing");
            gate::check(window.status_left_text().contains(QStringLiteral(
                            "manually schedule: a table / date format or custom-field formula "
                            "edit is pending")),
                        "Manually Schedule beside a pending date format: the status line "
                        "names it");
        } else {
            gate::check(click_manual(window, 1), "Manually Schedule clicks on task 1");
            gate::check(window.pending_edits().size() == 1,
                        "the Manually Schedule edit is queued once");
            gate::check(!queue_date_format(window, 5),
                        "the Table / Date Format dialog does not open beside a queued mode edit");
            gate::check(window.pending_edits().size() == 1,
                        "the Manually Schedule edit stays queued once");
            gate::check(window.status_left_text().contains(QStringLiteral(
                            "File > Save writes on its own, so it would refuse a table / date "
                            "format edit beside it")),
                        "a table / date format edit beside a queued mode edit: the status line "
                        "names it");
        }
        gate::check(window.document_dirty(),
                    (std::string("the one accepted edit is pending") + order).c_str());
    }

    // W35g-0.1 (R35fG1-1): beside a queued Manually Schedule (a second exclusive kind) Project >
    // Custom Fields... opens and cancels cleanly; an accepted Formula... is refused with the
    // queue unchanged.
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control opens for the Custom Fields case");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(click_manual(window, 1) && window.pending_edits().size() == 1,
                    "Manually Schedule is queued for the Custom Fields case");

        const CustomFieldsRun looked = run_custom_fields(window, false);
        gate::check(looked.dialog_seen, "Custom Fields opens beside a queued mode edit");
        gate::check(window.pending_edits().size() == 1,
                    "cancelling Custom Fields leaves the mode edit queued once");
        gate::check(window.status_left_text().contains(QStringLiteral("custom fields cancelled")),
                    "cancelling Custom Fields beside a queued mode edit: the status line says so");

        const CustomFieldsRun edited = run_custom_fields(window, true);
        gate::check(edited.dialog_seen, "Custom Fields opens again beside a queued mode edit");
        gate::check(!edited.input_seen, "Formula... beside a queued mode edit opens no input dialog");
        gate::check(window.pending_edits().size() == 1,
                    "a refused formula leaves the mode edit queued once");
        gate::check(window.status_left_text().contains(QStringLiteral(
                        "File > Save writes on its own, so it would refuse a custom-field "
                        "formula edit beside it")),
                    "Formula... beside a queued mode edit: the status line names the refusal");
    }
    return gate::finish();
}
