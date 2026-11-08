// app_qt/tests/task_mode_document_edit_gate_test.cpp -- wave 35 (W35-0.2, RW34b-3).
//
// THE RULE THIS GATES
//   A Manually Schedule edit and a pending table / date-format edit are two
//   different writers over one file; no Project pair measures them together, so
//   the shared save cascade refuses the combination before the document-edit
//   hook or any write (R34b-6, app::save_structural_edits).  The shell must show
//   that refusal, keep both edits queued, and write no file.
//
// WHAT IT DRIVES (all through the shell's own controls)
//   * wave30-task-mode-auto-control.mpp: Project's own save of an automatically
//     scheduled task;
//   * Task > Tasks > Manually Schedule on task 1 (queues one edit);
//   * Table > Date Format on the first table (queues the document edit), in
//     both orders;
//   * File > Save to a path under the build tree.
//
// Usage: qt_task_mode_document_edit_gate <wave30-task-mode-auto-control.mpp> <out-dir>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
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

    // Both orders: the cascade sees the same two pending edits either way.
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
        }
        gate::check(click_manual(window, 1), "Manually Schedule clicks on task 1");
        if (!format_first) {
            gate::check(queue_date_format(window, 5), "the Table / Date Format dialog queues");
        }
        gate::check(window.pending_edits().size() == 1,
                    "the Manually Schedule edit is queued once");
        gate::check(window.document_dirty(), (std::string("both edits are pending") + order).c_str());

        QString save_error;
        const bool saved = window.save_document_path(target, &save_error);
        gate::check(!saved, (std::string("the combined save is refused") + order).c_str());
        gate::check(save_error.contains(QStringLiteral(
                        "a task-mode edit cannot be combined with a table format or "
                        "custom-field formula edit")),
                    (std::string("the refusal names the combination") + order).c_str());
        gate::check(!std::filesystem::exists(target.toStdString()),
                    (std::string("the refused save writes no file") + order).c_str());
        gate::check(window.document_dirty(),
                    (std::string("the refused save keeps both edits pending") + order).c_str());
    }
    return gate::finish();
}
