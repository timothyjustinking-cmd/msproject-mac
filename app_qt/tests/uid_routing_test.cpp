// app_qt/tests/uid_routing_test.cpp -- wave 24 (W24-1/R1/R5/R6/R7).
//
// THE DEFECT THIS GATES
//   Wave 23 put a synthetic project-summary row at RecordSet row 0, but
//   `PlanDocument::plan().tasks` has no such row.  Every call site that used
//   `plan().tasks[selected.row]` then acted on the task BELOW the selection.
//   Custom Fields opened on (and Save wrote to) the wrong task; Delete named
//   the wrong task; the row menu read the wrong Auto/Manual state; the printed
//   Gantt was one row off; the Insert Task placeholder copied a task the writer
//   does not clone.
//
// THE FIXTURE
//   tests/data/wave24-order.mpp stores the project row SHOWN (214/Props
//   0x03400009 = 1), stores its FixedData rows out of ID order, and mixes
//   scheduling modes: ID 1 Alpha is auto, ID 2 Inserted Row is manual and has
//   a 4-day duration; the other tasks are 1-day manual rows.  So with the
//   project row at RecordSet 0, row+1 arithmetic lands on Inserted Row for row
//   1 -- a different task, manual and 4 days long.  Every assertion below
//   fails under that arithmetic.
//
// Usage: qt_uid_routing <wave24-order.mpp> <out-dir>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QModelIndex>
#include <QPushButton>
#include <QTableView>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "dialogs.h"
#include "field_link_edits.h"
#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "page_layout.h"
#include "plan_document.h"
#include "print_page.h"
#include "reader.h"
#include "shell_style.h"

namespace {

constexpr std::uint32_t kProjectRow = 0;

std::uint32_t uid_of_id(const mpp::Document& document, std::uint32_t id) {
    for (const mpp::Task& task : document.tasks) {
        if (task.row_id == id) {
            return task.uid;
        }
    }
    return 0;
}

const mpp::Task* task_of_id(const mpp::Document& document, std::uint32_t id) {
    for (const mpp::Task& task : document.tasks) {
        if (task.row_id == id) {
            return &task;
        }
    }
    return nullptr;
}

std::size_t row_of_uid(const core::sheet::SheetModel& sheet, std::uint32_t uid) {
    for (std::size_t row = 0; row < sheet.row_count(); ++row) {
        if (sheet.row_uid(row) == uid) {
            return row;
        }
    }
    return core::sheet::kNoIndex;
}

void select_row(msqt::MainWindow& window, std::size_t record_row) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return;
    }
    const std::size_t column =
        window.sheet_table_model()->visible_column_for_field(app::name_field_id());
    sheet->select_cell(record_row, column == core::sheet::kNoIndex ? 0 : column);
    window.refresh_menu_state();
}

std::string cell_text(msqt::MainWindow& window, std::size_t row,
                      const char* field_name) {
    const core::view::FieldId field = app::task_field_id(field_name);
    return window.sheet() != nullptr ? window.sheet()->field_text(row, field)
                                     : std::string();
}

// Finds the custom-field dialog the command opens, fills its first control and
// accepts it, so the real MainWindow::show_task_fields path runs.
struct FieldDialogResult {
    bool seen = false;
    bool filled = false;
};

void fill_and_accept_task_fields(FieldDialogResult* seen, const QString& text) {
    QTimer::singleShot(0, [seen, text]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            // TaskFieldsDialog has no Q_OBJECT (the shell defines no custom
            // signals), so identify it with RTTI rather than qobject_cast.
            auto* dialog = dynamic_cast<msqt::TaskFieldsDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible()) {
                continue;
            }
            seen->seen = true;
            if (!dialog->inputs().empty()) {
                dialog->inputs().front()->setText(text);
                seen->filled = true;
            }
            dialog->accept();
            return;
        }
    });
}

struct MessageBoxResult {
    bool seen = false;
    QString text;
};

void capture_message_box_and_reject(MessageBoxResult* seen) {
    QTimer::singleShot(0, [seen]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (box == nullptr || !box->isVisible()) {
                continue;
            }
            seen->seen = true;
            seen->text = box->text();
            if (QAbstractButton* no = box->button(QMessageBox::No)) {
                no->click();
            } else {
                box->reject();
            }
            return;
        }
    });
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

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_uid_routing <wave24-order.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);

    const mpp::Document file = mpp::load_file(fixture.toStdString());
    const std::uint32_t alpha_uid = uid_of_id(file, 1);
    const std::uint32_t inserted_uid = uid_of_id(file, 2);
    const std::uint32_t delta_uid = uid_of_id(file, 5);
    gate::check(alpha_uid != 0 && inserted_uid != 0 && delta_uid != 0,
                "the fixture has IDs 1, 2 and 5");
    const mpp::Task* alpha = task_of_id(file, 1);
    const mpp::Task* inserted = task_of_id(file, 2);
    gate::check(alpha != nullptr && !alpha->manually_scheduled,
                "ID 1 Alpha is auto-scheduled in the file");
    gate::check(inserted != nullptr && inserted->manually_scheduled,
                "ID 2 Inserted Row is manually scheduled in the file");

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    if (!window.open_file(fixture, &error)) {
        std::fprintf(stderr, "FAIL: could not open %s: %s\n", argv[1],
                     error.toUtf8().constData());
        return 1;
    }
    const std::size_t row_count =
        window.sheet() != nullptr ? window.sheet()->row_count() : 0;
    gate::check(row_count == 6, "the sheet shows the project row + five tasks");
    gate::check(window.sheet() != nullptr && window.sheet()->row_uid(0) == kProjectRow,
                "RecordSet row 0 is the project row");
    gate::check(window.sheet() != nullptr && window.sheet()->row_uid(1) == alpha_uid,
                "RecordSet row 1 is ID 1 (not index 1 of plan().tasks)");

    // --- 1. Task Information reads the SELECTED task ------------------------
    select_row(window, 1);
    msqt::TaskInformationValues values;
    gate::check(window.task_information_values(&values),
                "Task Information reads the selected task");
    gate::check(QString::fromStdString(values.name) == QStringLiteral("Alpha"),
                "row 1's Task Information is Alpha, the task on that row");
    gate::check(QString::fromStdString(values.name) != QStringLiteral("Inserted Row"),
                "row 1's Task Information is not the task BELOW it");

    // --- 2. the project row is not editable ---------------------------------
    select_row(window, 0);
    gate::check(!window.task_information_values(&values),
                "the project row has no Task Information values");
    msqt::TaskInformationValues project_values;
    project_values.name = QStringLiteral("renamed project row").toStdString();
    gate::check(!window.apply_task_information(project_values),
                "Task Information refuses the project row");
    gate::check(!window.document_dirty(),
                "a refused project-row edit does not mark the document dirty");

    // --- 3. Custom Fields: a save-and-reread proves the uid -----------------
    select_row(window, 1);
    FieldDialogResult field_seen;
    fill_and_accept_task_fields(&field_seen, QStringLiteral("ROUTE-ALPHA"));
    QAction* custom = gate::find_action(window, app::MenuAction::custom_fields);
    gate::check(custom != nullptr, "the Custom Fields command exists");
    if (custom != nullptr) {
        custom->trigger();
    }
    gate::check(field_seen.seen && field_seen.filled,
                "the Custom Fields dialog opened and was filled");
    gate::check(window.document_dirty(), "the queued field edit is a pending save");

    const QString saved = out_dir + QStringLiteral("/uid-routing.mpp");
    gate::check(window.save_document_path(saved, &error),
                "the pending field edit saves through the shared writer");
    {
        const mpp::Document reread = mpp::load_file(saved.toStdString());
        const mpp::Task* alpha_saved = task_of_id(reread, 1);
        const mpp::Task* inserted_saved = task_of_id(reread, 2);
        std::string text_on_alpha;
        std::string text_on_inserted;
        const std::uint32_t text1 = app::task_field_specs().front().field_id;
        if (alpha_saved != nullptr) {
            for (const mpp::CustomFieldValue& value : alpha_saved->custom_fields) {
                if (value.field_id == text1 && value.present) {
                    text_on_alpha = value.text;
                }
            }
        }
        if (inserted_saved != nullptr) {
            for (const mpp::CustomFieldValue& value : inserted_saved->custom_fields) {
                if (value.field_id == text1 && value.present) {
                    text_on_inserted = value.text;
                }
            }
        }
        gate::check(text_on_alpha == "ROUTE-ALPHA",
                    "Custom Fields wrote the value to the SELECTED task (ID 1)");
        gate::check(text_on_inserted.empty(),
                    "the task BELOW the selection did not receive the value");
    }

    // --- 4. Delete names the selected task ----------------------------------
    window.open_file(fixture, &error);
    select_row(window, 1);
    MessageBoxResult delete_box;
    capture_message_box_and_reject(&delete_box);
    QAction* remove = gate::find_action(window, app::MenuAction::delete_task);
    gate::check(remove != nullptr, "the Delete Task command exists");
    if (remove != nullptr) {
        remove->trigger();
    }
    gate::check(delete_box.seen, "Delete Task asks for confirmation");
    gate::check(delete_box.text.contains(QStringLiteral("Alpha")),
                "the confirmation names the SELECTED task (Alpha)");
    gate::check(!delete_box.text.contains(QStringLiteral("Inserted Row")),
                "the confirmation does not name the task BELOW the selection");
    gate::check(!window.document_dirty(), "rejecting the delete queues nothing");

    // --- 5. the row menu's Auto/Manual label reads the selected task --------
    select_row(window, 1);  // Alpha, auto in the file
    {
        QMenu* menu = window.build_row_menu();
        bool has_manual_command = false;
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Manually Schedule")) {
                has_manual_command = true;
            }
            gate::check(action->text() != QStringLiteral("Auto Schedule") ||
                            !action->isEnabled(),
                        "an auto task's menu does not offer Auto Schedule");
        }
        gate::check(has_manual_command,
                    "an auto task's menu offers Manually Schedule");
        delete menu;
    }
    select_row(window, 2);  // Inserted Row, manual in the file
    {
        QMenu* menu = window.build_row_menu();
        bool has_auto_command = false;
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Auto Schedule")) {
                has_auto_command = true;
            }
        }
        gate::check(has_auto_command,
                    "a manual task's menu offers Auto Schedule");
        delete menu;
    }

    // --- 6. the printed chart aligns with the printed sheet -----------------
    {
        const std::vector<std::size_t> rows =
            msqt::print_rows(window.sheet()->row_count(), {}, false);
        const app::PrintJob job = msqt::make_print_job(
            window.plan(), rows, 5, std::string(), std::string(), app::ZoomLevel::week);
        gate::check(job.subtitle.find("5 of 5 tasks") != std::string::npos,
                    "the print subtitle counts tasks, not the project row");
        const core::gantt::GanttLayout gantt =
            msqt::print_gantt(window.plan(), *window.sheet(), rows, job.spec,
                              app::ZoomLevel::week);
        gate::check(gantt.bars.size() == rows.size(),
                    "the printed chart has one bar per printed sheet row");
        for (std::size_t index = 0; index < rows.size() && index < gantt.bars.size();
             ++index) {
            gate::check(gantt.bars[index].row == static_cast<int>(index),
                        "printed chart row N is printed sheet row N");
            gate::check(gantt.bars[index].task_uid == window.sheet()->row_uid(rows[index]),
                        "printed chart row N carries its sheet row's uid");
        }
    }

    // --- 7. Insert Task's pending row equals what Save writes ---------------
    window.open_file(fixture, &error);
    select_row(window, 1);
    std::uint32_t max_uid = 0;
    for (const mpp::Task& task : file.tasks) {
        max_uid = std::max(max_uid, task.uid);
    }
    const std::uint32_t placeholder_uid = max_uid + 1;
    InputDialogResult input_seen;
    fill_and_accept_input_dialog(&input_seen, QStringLiteral("Pending Task"));
    QAction* insert = gate::find_action(window, app::MenuAction::insert_task);
    gate::check(insert != nullptr, "the Insert Task command exists");
    if (insert != nullptr) {
        insert->trigger();
    }
    gate::check(input_seen.seen, "Insert Task asks for a name");
    const std::size_t placeholder_row = window.sheet() != nullptr
                                            ? row_of_uid(*window.sheet(), placeholder_uid)
                                            : core::sheet::kNoIndex;
    gate::check(placeholder_row != core::sheet::kNoIndex,
                "the pending insert is displayed as a row");
    const std::string pending_duration =
        placeholder_row != core::sheet::kNoIndex
            ? cell_text(window, placeholder_row, "Duration")
            : std::string();
    const std::string pending_start =
        placeholder_row != core::sheet::kNoIndex
            ? cell_text(window, placeholder_row, "Start")
            : std::string();
    const std::size_t template_row =
        window.sheet() != nullptr ? row_of_uid(*window.sheet(), inserted_uid)
                                  : core::sheet::kNoIndex;
    const std::string template_duration =
        template_row != core::sheet::kNoIndex
            ? cell_text(window, template_row, "Duration")
            : std::string();
    const std::string template_start =
        template_row != core::sheet::kNoIndex
            ? cell_text(window, template_row, "Start")
            : std::string();
    gate::check(!pending_duration.empty(),
                "the pending insert has a displayed duration");
    gate::check(pending_duration == template_duration,
                "the pending row copies the LAST STORED row's duration (what the writer clones)");
    gate::check(pending_start == template_start,
                "the pending row copies the LAST STORED row's start");

    const QString inserted_saved = out_dir + QStringLiteral("/uid-insert.mpp");
    gate::check(window.save_document_path(inserted_saved, &error),
                "the pending insert saves through the shared writer");
    {
        const mpp::Document reread = mpp::load_file(inserted_saved.toStdString());
        const mpp::Task* new_task = nullptr;
        for (const mpp::Task& task : reread.tasks) {
            if (task.row_id == 6) {
                new_task = &task;
            }
        }
        gate::check(new_task != nullptr, "the saved file has the new task (ID 6)");
        if (new_task != nullptr) {
            gate::check(new_task->duration_ticks / 10u == 1920u,
                        "Save writes the 4-day template duration, not Delta's 1 day");
            const std::uint32_t new_uid = new_task->uid;
            gate::check(window.open_file(inserted_saved, &error),
                        "the saved file reopens");
            const std::size_t new_row = window.sheet() != nullptr
                                            ? row_of_uid(*window.sheet(), new_uid)
                                            : core::sheet::kNoIndex;
            gate::check(new_row != core::sheet::kNoIndex,
                        "the saved task is a displayed row");
            gate::check(new_row != core::sheet::kNoIndex &&
                            cell_text(window, new_row, "Duration") == pending_duration,
                        "the pending row's duration equals what Save wrote");
            gate::check(new_row != core::sheet::kNoIndex &&
                            cell_text(window, new_row, "Start") == pending_start,
                        "the pending row's start equals what Save wrote");
        }
    }
    return gate::finish();
}
