// app_qt/tests/delete_summary_test.cpp -- wave 33 (W33-1.1 / A-3, High).
//
// THE DEFECT THIS GATES
//   Delete Task accepted a summary row in every path.  The writer tombstones
//   the summary alone; its live subtasks keep a parent identity pointing at the
//   tombstone, a shape no Project pair measured.  The fix refuses in push(), in
//   delete_task_row() and in both shells with "delete the subtasks first, or
//   outdent them".
//
// THE FIXTURE
//   tests/data/wave28-outline-indent.mpp is Project's own save: Task 1 is the
//   summary of Task 2 (outline 1 / 1.1), Task 3 is a top-level leaf.  The test
//   copies it into the build tree; the committed file is never opened in place.
//
// WHAT IT PROVES
//   * Delete Task on the summary row shows no confirmation and queues nothing,
//     and the status line names the refusal;
//   * Delete Task on a leaf still asks for confirmation, so the refusal is not
//     a blanket disable;
//   * the pending-edit list and the dirty flag stay empty in the refused case.
//
// Usage: qt_delete_summary <wave28-outline-indent.mpp> <out-dir>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>

#include "gate_util.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "reader.h"
#include "sheet.h"
#include "shell_style.h"

namespace {

struct MessageBoxResult {
    bool seen = false;
    QString text;
};

// The shell asks with QMessageBox::question (Yes/No).  Accepting is deliberate:
// if the summary delete still reaches the confirmation, the pre-fix shell queues
// it and the "queues nothing" checks fail instead of merely observing a dialog.
void capture_message_box(MessageBoxResult* seen, bool accept) {
    QTimer::singleShot(0, [seen, accept]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (box == nullptr || !box->isVisible()) {
                continue;
            }
            seen->seen = true;
            seen->text = box->text();
            if (accept) {
                if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
                    yes->click();
                } else {
                    box->accept();
                }
            } else if (QAbstractButton* no = box->button(QMessageBox::No)) {
                no->click();
            } else {
                box->reject();
            }
            return;
        }
    });
}

std::uint32_t uid_of_id(const mpp::Document& document, std::uint32_t id) {
    for (const mpp::Task& task : document.tasks) {
        if (task.row_id == id) {
            return task.uid;
        }
    }
    return 0;
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
    if (sheet == nullptr || record_row == core::sheet::kNoIndex) {
        return;
    }
    const std::size_t column =
        window.sheet_table_model()->visible_column_for_field(app::name_field_id());
    sheet->select_cell(record_row, column == core::sheet::kNoIndex ? 0 : column);
    window.refresh_menu_state();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_delete_summary <wave28-outline-indent.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);

    // A missing committed fixture FAILS the gate (wave-33 brief section 1).
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed fixture wave28-outline-indent.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }

    const mpp::Document file = mpp::load_file(fixture.toStdString());
    const std::uint32_t summary_uid = uid_of_id(file, 1);
    const std::uint32_t leaf_uid = uid_of_id(file, 3);
    const mpp::Task* summary = nullptr;
    for (const mpp::Task& task : file.tasks) {
        if (task.uid == summary_uid) {
            summary = &task;
        }
    }
    gate::check(summary_uid != 0 && leaf_uid != 0,
                "the fixture stores IDs 1 and 3");
    gate::check(summary != nullptr && summary->summary,
                "the fixture's ID 1 is a summary (Task 2 is its child)");

    const QString copy = out_dir + QStringLiteral("/w33-delete-summary.mpp");
    std::error_code copy_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing,
                               copy_error);
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

    QAction* remove = gate::find_action(window, app::MenuAction::delete_task);
    gate::check(remove != nullptr, "the Delete Task command exists");
    if (remove == nullptr) {
        return gate::finish();
    }

    // --- the summary row is refused before any confirmation -----------------
    select_row(window, row_of_uid(*window.sheet(), summary_uid));
    MessageBoxResult summary_box;
    capture_message_box(&summary_box, true);
    remove->trigger();
    // A refusal returns without opening a modal loop, so the capture timer
    // above is still queued.  Run it now: it must find no dialog, and the next
    // capture (leaf section) must not inherit a stale accept.
    QApplication::processEvents();
    gate::check(!summary_box.seen,
                "the summary delete shows no confirmation dialog");
    gate::check(window.pending_edits().empty(),
                "the summary delete queues no edit");
    gate::check(!window.document_dirty(),
                "the refused summary delete leaves the document clean");
    gate::check(
        window.status_left_text().contains(
            QStringLiteral("delete the subtasks first, or outdent them")),
        "the status line names the summary-delete refusal");

    // A fresh load so a pre-fix run cannot leave a queued delete behind.
    gate::check(window.open_file(copy, &error),
                "the fixture reopens before the leaf check");
    if (window.sheet() == nullptr) {
        return gate::finish();
    }
    QAction* remove_leaf = gate::find_action(window, app::MenuAction::delete_task);
    gate::check(remove_leaf != nullptr,
                "the Delete Task command survives the reopen");
    const std::size_t leaf_row = row_of_uid(*window.sheet(), leaf_uid);
    gate::check(leaf_row != core::sheet::kNoIndex,
                "the leaf row is visible after the reopen");
    select_row(window, leaf_row);
    gate::check(remove_leaf != nullptr && remove_leaf->isEnabled(),
                "the leaf Delete Task command is enabled");

    // --- a leaf still reaches the confirmation ------------------------------
    MessageBoxResult leaf_box;
    capture_message_box(&leaf_box, false);
    if (remove_leaf != nullptr) {
        remove_leaf->trigger();
    }
    gate::check(leaf_box.seen,
                "a leaf delete still asks for confirmation");
    gate::check(window.pending_edits().empty(),
                "rejecting the leaf delete queues nothing");

    return gate::finish();
}
