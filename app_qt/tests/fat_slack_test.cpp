// app_qt/tests/fat_slack_test.cpp -- wave 33 (W33-1.7 / A-8 / D-7, Medium).
//
// THE DEFECT THIS GATES
//   MS Project can leave sectors after the FAT's last entry.  The reader says
//   so in its own warning ("N unreferenced sectors"), and mppwrite's
//   CfbDocument::to_bytes refuses to re-emit such a container.  The shells
//   ignored the warning: the file opened, every edit was accepted and shown,
//   and the failure only appeared at Save -- the whole session's work was
//   unwritable.
//
// THE FIX
//   At open, both shells detect the reader's warning and mark the document
//   read-only for the session with the reason "saved by Project with
//   unreferenced sectors; saving is not supported yet".  The status line shows
//   it, every edit entry point refuses with it (before any dialog), the cell
//   and structural edit models refuse as a backstop, and Save refuses before
//   the prompt.
//
// THE FIXTURE
//   tests/data/wave33-fat-slack.mpp is synthetic-base.mpp plus 64 zero sectors
//   (270336 bytes): the FAT describes 512 of the file's 527 physical sectors
//   and 15 belong to no chain.  The test copies it into the build tree; the
//   committed file is never opened in place.
//
// WHAT IT PROVES
//   * the opened document's status line names the reason, and no pending edit
//     exists;
//   * a cell edit is refused with the reason, the cell keeps its text and no
//     session stays open;
//   * Insert Task and Delete Task refuse with the reason and never open a
//     dialog (a dialog on the pre-fix tree is what this gate records);
//   * Save to a new path refuses with the reason and writes no file;
//   * the clean fixture (the slack file is its bytes plus appended sectors)
//     still accepts the same cell edit, so the guard is not a constant.
//
// Usage: qt_fat_slack <wave33-fat-slack.mpp> <synthetic-base.mpp> <out-dir>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QModelIndex>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "gate_util.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "sheet.h"
#include "sheet_table_model.h"
#include "shell_style.h"

namespace {

const QString kReason = QStringLiteral(
    "saved by Project with unreferenced sectors; saving is not supported yet");

// Rejects any modal dialog that a pre-fix edit handler opens, so the RED run
// records the dialog instead of hanging in it.  A handler the fix refuses
// before any dialog leaves `seen` false.
struct ModalProbe {
    bool seen = false;
    QString title;
    QTimer poll;
    QTimer watchdog;

    void scan() {
        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* candidate = qobject_cast<QDialog*>(widget);
                if (candidate != nullptr && candidate->isVisible() && candidate->isModal()) {
                    dialog = candidate;
                    break;
                }
            }
        }
        if (dialog == nullptr) {
            return;
        }
        seen = true;
        title = dialog->windowTitle();
        dialog->reject();
    }

    void arm() {
        seen = false;
        title.clear();
        poll.setInterval(5);
        QObject::connect(&poll, &QTimer::timeout, [this]() { scan(); });
        poll.start();
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, [this]() {
            scan();
            if (QWidget* active = QApplication::activeModalWidget(); active != nullptr) {
                active->close();
            }
        });
        watchdog.start(5000);
    }

    void disarm() {
        poll.stop();
        watchdog.stop();
    }
};

bool copy_fixture(const QString& fixture, const QString& target) {
    std::error_code error;
    std::filesystem::copy_file(fixture.toStdString(), target.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

// The first displayed row that is a real task (uid 0 is the project row).
int first_task_row(const msqt::SheetTableModel& model) {
    const core::sheet::SheetModel* sheet = model.sheet();
    if (sheet == nullptr) {
        return -1;
    }
    for (int row = 0; row < model.rowCount(); ++row) {
        const std::size_t record = model.record_row(row);
        if (record != core::sheet::kNoIndex && sheet->row_uid(record) != 0) {
            return row;
        }
    }
    return -1;
}

void trigger_action(msqt::MainWindow& window, app::MenuAction id, ModalProbe* probe) {
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr && action->isEnabled(), "the menu action is live");
    if (action == nullptr) {
        return;
    }
    if (probe != nullptr) {
        probe->arm();
    }
    action->trigger();
    QApplication::processEvents();
    if (probe != nullptr) {
        probe->disarm();
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: qt_fat_slack <wave33-fat-slack.mpp> <synthetic-base.mpp> "
                     "<out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString clean_fixture = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = QString::fromLocal8Bit(argv[3]);

    // A missing committed fixture FAILS the gate (wave-33 brief section 1).
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed fixture wave33-fat-slack.mpp is present");
    gate::check(std::filesystem::exists(clean_fixture.toStdString()),
                "the committed fixture synthetic-base.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString()) ||
        !std::filesystem::exists(clean_fixture.toStdString())) {
        return gate::finish();
    }

    const QString copy = out_dir + QStringLiteral("/w33-fat-slack-source.mpp");
    const QString clean_copy = out_dir + QStringLiteral("/w33-fat-slack-clean.mpp");
    gate::check(copy_fixture(fixture, copy), "the slack fixture copy is written");
    gate::check(copy_fixture(clean_fixture, clean_copy), "the clean fixture copy is written");

    // --- the slack document is read-only from the moment it opens -------------
    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(copy, &error), "the slack fixture opens");
    window.show();
    QApplication::processEvents();
    if (window.sheet() == nullptr) {
        return gate::finish();
    }

    gate::check(window.status_left_text() == kReason,
                "the status line names the read-only reason at open");
    gate::check(window.pending_edits().empty(), "a freshly opened slack document has no edits");

    const int task_row = first_task_row(*window.sheet_table_model());
    const int name_column =
        window.sheet_table_model()->display_column_for_field(app::name_field_id());
    gate::check(task_row >= 0, "the fixture shows a task row (not only the project row)");
    gate::check(name_column >= 0, "the fixture shows a Name column");
    if (task_row < 0 || name_column < 0) {
        return gate::finish();
    }
    const std::size_t task_record = window.sheet_table_model()->record_row(task_row);
    const std::size_t name_model_column =
        window.sheet_table_model()->model_column(name_column);
    const std::string before =
        window.sheet()->cell_text(task_record, name_model_column);

    // --- a cell edit is refused before a session opens ------------------------
    const bool cell_edit =
        window.sheet_table_model()->setData(window.sheet_table_model()->index(task_row,
                                                                              name_column),
                                            QStringLiteral("W33 edit"), Qt::EditRole);
    gate::check(!cell_edit, "a read-only document refuses a sheet cell edit");
    gate::check(window.status_left_text() == kReason,
                "the cell refusal puts the read-only reason on the status line");
    gate::check(!window.sheet()->editing(), "no edit session is left open by the refusal");
    gate::check(window.sheet()->cell_text(task_record, name_model_column) == before,
                "the refused cell edit leaves the stored text unchanged");
    gate::check(!window.document_dirty(), "the refused cell edit leaves nothing to save");

    // --- Insert Task refuses before the name dialog ---------------------------
    window.sheet()->select_cell(task_record, name_model_column);
    ModalProbe insert_probe;
    trigger_action(window, app::MenuAction::insert_task, &insert_probe);
    gate::check(!insert_probe.seen,
                "the read-only Insert Task refusal never opens a dialog");
    gate::check(window.pending_edits().empty(), "the refused insert queues nothing");
    gate::check(window.status_left_text() == kReason,
                "the insert refusal names the read-only reason");

    // --- Delete Task refuses before the confirmation --------------------------
    window.sheet()->select_cell(task_record, name_model_column);
    ModalProbe delete_probe;
    trigger_action(window, app::MenuAction::delete_task, &delete_probe);
    gate::check(!delete_probe.seen,
                "the read-only Delete Task refusal never opens a confirmation");
    gate::check(window.pending_edits().empty(), "the refused delete queues nothing");
    gate::check(window.status_left_text() == kReason,
                "the delete refusal names the read-only reason");

    // --- Save refuses before the prompt and writes nothing --------------------
    const QString target = out_dir + QStringLiteral("/w33-fat-slack-save.mpp");
    std::error_code remove_error;
    std::filesystem::remove(target.toStdString(), remove_error);
    QString save_error;
    gate::check(!window.save_document_path(target, &save_error),
                "a read-only session refuses Save");
    gate::check(save_error == kReason, "the Save refusal carries the read-only reason");
    gate::check(!std::filesystem::exists(target.toStdString()),
                "the refused Save writes no file");
    gate::check(window.status_left_text() == kReason,
                "the Save refusal puts the reason on the status line");

    // --- the clean fixture is NOT read-only: the same edit is accepted --------
    msqt::MainWindow clean_window;
    clean_window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString clean_error;
    gate::check(clean_window.open_file(clean_copy, &clean_error), "the clean fixture opens");
    clean_window.show();
    QApplication::processEvents();
    if (clean_window.sheet() == nullptr) {
        return gate::finish();
    }
    gate::check(clean_window.status_left_text() != kReason,
                "a clean document does not show the read-only reason");
    const int clean_row = first_task_row(*clean_window.sheet_table_model());
    const int clean_name_column =
        clean_window.sheet_table_model()->display_column_for_field(app::name_field_id());
    gate::check(clean_row >= 0 && clean_name_column >= 0,
                "the clean fixture shows a task row and a Name column");
    if (clean_row >= 0 && clean_name_column >= 0) {
        const std::size_t clean_record =
            clean_window.sheet_table_model()->record_row(clean_row);
        const std::size_t clean_model_column =
            clean_window.sheet_table_model()->model_column(clean_name_column);
        const std::string clean_before =
            clean_window.sheet()->cell_text(clean_record, clean_model_column);
        const bool clean_edit = clean_window.sheet_table_model()->setData(
            clean_window.sheet_table_model()->index(clean_row, clean_name_column),
            QStringLiteral("W33 clean edit"), Qt::EditRole);
        gate::check(clean_edit, "a clean document accepts the same cell edit");
        gate::check(clean_window.sheet()->cell_text(clean_record, clean_model_column) !=
                        clean_before,
                    "the accepted clean edit changes the cell");
    }

    return gate::finish();
}
