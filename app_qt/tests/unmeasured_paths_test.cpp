// app_qt/tests/unmeasured_paths_test.cpp -- wave 34 (W34-4.0).
//
// THE RULE THIS GATES
//   W34-0.5 measured Insert Task, Delete Task, link ADD and the custom-field
//   SET and CLEAR against Project on the synthetic base: the port's save differs
//   from Project's own save of the same edit in 15, 6, 13, 2 and 5 streams.
//   Standing rule 1 refuses each path up front, with the decision-9 reason in
//   the status line, and queues nothing.  This program NEVER lifts the refusal
//   (no allow_unmeasured_paths_for_tests call): it proves the shipped default.
//
// WHAT IT PROVES, in the real MainWindow
//   * Insert Task shows no name prompt, queues nothing and names the refusal;
//   * Delete Task shows no confirmation, queues nothing and names the refusal;
//   * Task Information's Custom Fields tab (a SET) is refused whole: nothing is
//     queued, the document stays clean, the status line names the refusal.
//   The CLEAR and the link ADD refusals are the shared builders'
//   (app::make_task_field_edits, app::validate_link_add), tested in
//   app_unmeasured_paths; the Predecessors dialog calls the same builder before
//   it queues.
//
// Usage: qt_unmeasured_paths <synthetic-base.mpp> <out-dir>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "dialogs.h"
#include "edit_model.h"
#include "field_link_edits.h"
#include "gate_util.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "reader.h"
#include "sheet.h"
#include "shell_style.h"

namespace {

struct DialogSeen {
    bool seen = false;
};

// Any modal box or dialog a refused command wrongly opens is closed (so the
// test cannot hang) and recorded.
void close_any_modal(DialogSeen* seen) {
    QTimer::singleShot(0, [seen]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible()) {
                continue;
            }
            seen->seen = true;
            dialog->reject();
        }
    });
}

void select_row(msqt::MainWindow& window, std::size_t record_row) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr || record_row == core::sheet::kNoIndex) {
        return;
    }
    const std::size_t column =
        window.sheet_table_model()->visible_column_for_field(app::name_field_id());
    sheet->select_cell(record_row, column == core::sheet::kNoIndex ? 0 : column);
    window.refresh_menu_state();
}

bool names_refusal(const msqt::MainWindow& window, const char* command) {
    const QString text = window.status_left_text();
    return text.contains(QStringLiteral("is not implemented in this port")) &&
           text.contains(QString::fromLatin1(command));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_unmeasured_paths <synthetic-base.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the refusal is on in this program (nothing lifts it)");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed fixture synthetic-base.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }

    const QString copy = out_dir + QStringLiteral("/w34-unmeasured-paths.mpp");
    std::error_code copy_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, copy_error);
    gate::check(!copy_error, "the fixture copy is written under the build tree");
    if (copy_error) {
        return gate::finish();
    }

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(copy, &error), "the copied fixture opens");
    if (window.sheet() == nullptr) {
        return gate::finish();
    }
    select_row(window, 1);

    // --- Insert Task ---------------------------------------------------------
    QAction* insert = gate::find_action(window, app::MenuAction::insert_task);
    gate::check(insert != nullptr, "the Insert Task command exists");
    if (insert != nullptr) {
        DialogSeen prompt;
        close_any_modal(&prompt);
        insert->trigger();
        QApplication::processEvents();
        gate::check(!prompt.seen, "Insert Task opens no name prompt");
        gate::check(window.pending_edits().empty(), "Insert Task queues no edit");
        gate::check(!window.document_dirty(), "the refused Insert Task leaves the document clean");
        gate::check(names_refusal(window, "Insert Task"),
                    "the status line names the Insert Task refusal");
    }

    // --- Delete Task ---------------------------------------------------------
    select_row(window, 1);
    QAction* remove = gate::find_action(window, app::MenuAction::delete_task);
    gate::check(remove != nullptr, "the Delete Task command exists");
    if (remove != nullptr) {
        DialogSeen confirm;
        close_any_modal(&confirm);
        remove->trigger();
        QApplication::processEvents();
        gate::check(!confirm.seen, "Delete Task shows no confirmation");
        gate::check(window.pending_edits().empty(), "Delete Task queues no edit");
        gate::check(!window.document_dirty(), "the refused Delete Task leaves the document clean");
        gate::check(names_refusal(window, "Delete Task"),
                    "the status line names the Delete Task refusal");
    }

    // --- Custom Fields: a SET -------------------------------------------------
    select_row(window, 1);
    msqt::TaskInformationValues values;
    gate::check(window.task_information_values(&values),
                "Task Information reads the selected task");
    const std::vector<app::TaskFieldSpec>& specs = app::task_field_specs();
    gate::check(!specs.empty(), "the Task Fields specs are present");
    if (!specs.empty() && !values.custom_field_ids.empty()) {
        values.custom_field_texts.assign(values.custom_field_ids.size(), std::string());
        values.custom_field_texts[0] = "W34-SET";
        gate::check(!window.apply_task_information(values),
                    "Task Information refuses a custom-field SET");
        gate::check(window.pending_edits().empty(), "the refused SET queues nothing");
        gate::check(!window.document_dirty(), "the refused SET leaves the document clean");
        gate::check(names_refusal(window, "Setting a custom field value"),
                    "the status line names the SET refusal");
    } else {
        gate::check(false, "Task Information carries custom-field ids");
    }

    return gate::finish();
}
