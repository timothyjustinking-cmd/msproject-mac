// app_qt/tests/save_reopen_test.cpp -- wave 19's acceptance gate: edit, save,
// reopen.
//
// WHAT IT PROVES (wave 19 brief item 6)
//   * a COPY of tests/data/synthetic-base.mpp is loaded into the real MainWindow
//     (the committed fixture is never opened in place and nothing is written
//     under tests/data/);
//   * a cell edit goes through the shell's own model path -- SheetTableModel::
//     setData -> SheetModel::begin_edit/commit_edit -- and Undo/Redo move the
//     real SheetModel stack through the menu actions;
//   * the dirty flag tracks exactly what a save would write;
//   * Save goes through the shared app::Document path to a new file, and the
//     edit is persisted (the writer's measured form: an SNET constraint, which
//     is what Project honours on open);
//   * reopening the saved file in the same real window yields the same plan
//     (row/bar counts, no remaining pending edits);
//   * a save with nothing pending writes the source container byte-identically
//     (Document's documented invariant).
//
// Everything is written under the build tree's test-out directory, and the test
// is offscreen like the rest (QT_QPA_PLATFORM=offscreen, QT_SCALE_FACTOR=1).
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QMenu>
#include <QMenuBar>
#include <QModelIndex>
#include <QString>
#include <QTableView>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "document.h"
#include "entry_columns.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "reader.h"
#include "sheet.h"
#include "shell_style.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

QAction* find_action(QMainWindow& window, app::MenuAction id) {
    QMenuBar* bar = window.menuBar();
    if (bar == nullptr) {
        return nullptr;
    }
    for (QAction* menu_action : bar->actions()) {
        QMenu* menu = menu_action->menu();
        if (menu == nullptr) {
            continue;
        }
        for (QAction* action : menu->actions()) {
            if (!action->data().isValid()) {
                continue;
            }
            if (action->data().toInt() == static_cast<int>(id)) {
                return action;
            }
        }
    }
    return nullptr;
}

std::vector<std::uint8_t> file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_save_reopen_tests <fixture.mpp> <out-dir>\n");
        return 2;
    }
    const std::filesystem::path fixture = argv[1];
    const std::filesystem::path out_dir = argv[2];
    std::error_code fs_error;
    std::filesystem::create_directories(out_dir, fs_error);

    // Never open or write the committed fixture in place: work on a build-tree
    // copy, exactly as the app's headless save tests do.
    const std::filesystem::path source = out_dir / "source.mpp";
    const std::filesystem::path saved = out_dir / "saved.mpp";
    const std::filesystem::path untouched = out_dir / "untouched.mpp";
    std::filesystem::copy_file(fixture, source,
                               std::filesystem::copy_options::overwrite_existing, fs_error);
    if (!std::filesystem::exists(source)) {
        std::fprintf(stderr, "FAIL: could not copy %s\n", fixture.string().c_str());
        return 1;
    }
    std::filesystem::remove(saved, fs_error);
    std::filesystem::remove(untouched, fs_error);

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    if (!window.open_file(QString::fromStdString(source.string()), &error)) {
        std::fprintf(stderr, "FAIL: could not open %s: %s\n", source.string().c_str(),
                     error.toUtf8().constData());
        return 1;
    }
    QApplication::processEvents();
    // Wave 23 (B-1): the sheet and Gantt show the project-summary row (ID 0)
    // ahead of the fixture's three tasks.
    check(window.sheet_row_count() == 4, "the copied fixture shows the project row + 3 tasks");
    check(window.gantt_bar_count() == 4, "the copied fixture has the project bar + 3 task bars");
    check(!window.document_dirty(), "a freshly opened plan is not dirty");

    QTableView* sheet = window.sheet_view();
    check(sheet != nullptr && sheet->model() != nullptr, "the sheet view has a model");
    if (sheet == nullptr || sheet->model() == nullptr) {
        std::printf("qt_save_reopen: %d failure(s)\n", g_failures);
        return 1;
    }
    auto* model = sheet->model();
    const int duration_column = msqt::entry_column_index(msqt::EntryCellKind::duration);
    const int start_column = msqt::entry_column_index(msqt::EntryCellKind::start);
    check(duration_column >= 0, "the Entry table has a Duration column");
    check(start_column >= 0, "the Entry table has a Start column");
    if (duration_column < 0 || start_column < 0) {
        std::printf("qt_save_reopen: %d failure(s)\n", g_failures);
        return 1;
    }

    // The task the Start edit applies to: the fixture's first task uid.
    std::uint32_t edited_uid = 0;
    try {
        const mpp::Document original = mpp::load_file(source.string());
        if (!original.tasks.empty()) {
            edited_uid = original.tasks.front().uid;
        }
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "FAIL: the fixture does not load: %s\n", failure.what());
        ++g_failures;
    }
    check(edited_uid != 0, "the fixture's first task has a UniqueID");

    // --- Undo/Redo through the real sheet stack and the menu actions ---------
    // Row 2 is the fixture's second task (row 0 is the project summary row,
    // row 1 the first task); "5d" changes the 3-day second task's text.
    const QModelIndex duration_index = model->index(2, duration_column);
    const QString duration_before = model->data(duration_index, Qt::DisplayRole).toString();
    check(model->setData(duration_index, QStringLiteral("5d"), Qt::EditRole),
          "a Duration edit commits through the shell's model path");
    const QString duration_after = model->data(duration_index, Qt::DisplayRole).toString();
    check(duration_after != duration_before, "the Duration cell shows the new value");
    check(!window.document_dirty(),
          "a view-only Duration edit does not claim unsaved file changes");

    QAction* undo_action = find_action(window, app::MenuAction::undo);
    QAction* redo_action = find_action(window, app::MenuAction::redo);
    check(undo_action != nullptr, "the menu has Undo");
    check(redo_action != nullptr, "the menu has Redo");
    if (undo_action != nullptr) {
        check(undo_action->isEnabled(), "Undo is enabled after an edit");
        undo_action->trigger();
        check(model->data(duration_index, Qt::DisplayRole).toString() == duration_before,
              "Undo restores the previous Duration");
    }
    if (redo_action != nullptr) {
        check(redo_action->isEnabled(), "Redo is enabled after an Undo");
        redo_action->trigger();
        check(model->data(duration_index, Qt::DisplayRole).toString() == duration_after,
              "Redo re-applies the Duration");
    }
    if (undo_action != nullptr) {
        undo_action->trigger();
        check(model->data(duration_index, Qt::DisplayRole).toString() == duration_before,
              "a second Undo returns to the original Duration");
    }

    // --- A Start edit: the one cell edit the file writer persists ------------
    const QModelIndex start_index = model->index(1, start_column);
    check(model->setData(start_index, QStringLiteral("2026-03-02 08:00"), Qt::EditRole),
          "a Start edit commits through the shell's model path");
    check(window.pending_start_edits() == 1, "exactly one start edit is pending");
    check(window.document_dirty(), "the start edit makes the document dirty");

    // --- Save through the shared Document path ------------------------------
    if (!window.save_document_path(QString::fromStdString(saved.string()), &error)) {
        std::fprintf(stderr, "FAIL: save failed: %s\n", error.toUtf8().constData());
        ++g_failures;
    } else {
        check(std::filesystem::exists(saved), "the saved file exists");
        check(window.last_save_rows_written() == 1, "the save wrote one start row");
        check(!window.document_dirty(), "the document is clean after a successful save");
    }

    // --- Reopen the SAVED file in the real window ---------------------------
    if (!window.open_file(QString::fromStdString(saved.string()), &error)) {
        std::fprintf(stderr, "FAIL: could not reopen %s: %s\n", saved.string().c_str(),
                     error.toUtf8().constData());
        ++g_failures;
    } else {
        QApplication::processEvents();
        check(window.sheet_row_count() == 4,
              "the saved file reopens with the project row + 3 task rows");
        check(window.gantt_bar_count() == 4,
              "the saved file reopens with the project bar + 3 task bars");
        check(window.loaded_task_count() == 3, "the saved file reopens with 3 tasks");
        check(!window.document_dirty(), "the reopened file has no pending edits");
        check(window.table_widths_read(), "the saved file keeps its 214/CTable widths");
    }

    // --- The edit persisted, in the writer's measured form -------------------
    // pin_task_start stores an SNET constraint (PjConstraint 4); that is the
    // store Project was measured to honour on open (docs/audit-handoff.md 4.2).
    const core::sched::date32 expected_start = core::sched::from_ymd(2026, 3, 2, 8, 0);
    try {
        const mpp::Document reread = mpp::load_file(saved.string());
        check(reread.tasks.size() == 3, "the saved file reads back 3 tasks");
        const mpp::Task* task = nullptr;
        for (const mpp::Task& candidate : reread.tasks) {
            if (candidate.uid == edited_uid) {
                task = &candidate;
                break;
            }
        }
        if (task == nullptr) {
            check(false, "the edited task is in the saved file");
        } else {
            check(task->constraint_type == 4, "the saved Start is pinned by an SNET constraint");
            check(task->constraint_date.raw == app::packed_from_date32(expected_start),
                  "the constraint date is the edited Start");
        }
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "FAIL: the saved file does not load: %s\n", failure.what());
        ++g_failures;
    }

    // --- A save with nothing pending is byte-identical ----------------------
    app::Document document(source.string());
    const app::SaveOutcome clean = document.save(untouched.string());
    check(clean.ok, "a save with nothing pending succeeds");
    if (clean.ok) {
        check(file_bytes(source) == file_bytes(untouched),
              "a save with nothing pending writes the source container byte-identically");
    }

    // The committed fixture was never touched: the source copy is still there.
    check(std::filesystem::exists(source), "the build-tree source copy still exists");

    std::printf("qt_save_reopen: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
