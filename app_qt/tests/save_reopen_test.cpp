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
#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QFile>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QTableView>
#include <QTimer>
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

#include "backstage.h"
#include "document.h"
#include "entry_columns.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "preferences_store.h"
#include "reader.h"
#include "ribbon_bar.h"
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

QCheckBox* find_ribbon_checkbox(msqt::RibbonBar* ribbon, const QString& tab,
                                const QString& group, const QString& label) {
    if (ribbon == nullptr) {
        return nullptr;
    }
    const QStringList tabs = ribbon->tab_titles();
    const int tab_index = tabs.indexOf(tab);
    if (tab_index < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(tab_index);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int group_index = groups.indexOf(group);
    if (group_index < 0) {
        return nullptr;
    }
    for (int index = 0; index < ribbon->group_control_count(group_index); ++index) {
        auto* box = qobject_cast<QCheckBox*>(ribbon->group_control(group_index, index));
        if (box == nullptr) {
            continue;
        }
        QString text = box->text();
        text.remove(QLatin1Char('&'));
        if (text == label) {
            return box;
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
    // Wave 24 (W24-2): the fixture stores 214/Props 0x03400009 = 0, so it has
    // three task rows and no project-summary row.
    check(window.sheet_row_count() == 3, "the copied fixture shows its three tasks");
    check(window.gantt_bar_count() == 3, "the copied fixture has three task bars");
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
    // Row 1 is the fixture's second task (no project row is shown); "5d"
    // changes the 3-day second task's text.
    const QModelIndex duration_index = model->index(1, duration_column);
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
    // Row 0 is the first task now that the file hides the project row.
    const QModelIndex start_index = model->index(0, start_column);
    check(model->setData(start_index, QStringLiteral("2026-03-02 08:00"), Qt::EditRole),
          "a Start edit commits through the shell's model path");
    check(window.pending_start_edits() == 1, "exactly one start edit is pending");
    check(window.document_dirty(), "the start edit makes the document dirty");

    // --- W26-REV-2: a project-summary toggle must not drop a pending cell edit.
    // The RecordSet is the source of truth for cell edits; the toggle rebuilds
    // from the plan, so it must fold the records back first.  Before the fix,
    // pending_start_edits() fell to 0 here and the save wrote no start row.
    QCheckBox* summary_box = find_ribbon_checkbox(
        window.ribbon(), QStringLiteral("Gantt Chart Format"), QStringLiteral("Show/Hide"),
        QStringLiteral("Project Summary Task"));
    check(summary_box != nullptr, "the Project Summary Task check box is live");
    const std::size_t rows_before_toggle = window.sheet_row_count();
    if (summary_box != nullptr) {
        summary_box->click();
        QApplication::processEvents();
        check(window.project_summary_visible(), "the toggle shows the project row");
        check(window.sheet_row_count() == rows_before_toggle + 1,
              "the project row is the one row added");
        check(window.pending_start_edits() == 1,
              "the pending Start edit survives the project-summary toggle");
        check(window.document_dirty(), "the document stays dirty after the toggle");
        summary_box->click();
        QApplication::processEvents();
        check(!window.project_summary_visible(), "the second click hides the row again");
        check(window.pending_start_edits() == 1,
              "the pending Start edit survives the second toggle");
        check(window.sheet_row_count() == rows_before_toggle,
              "the sheet returns to its original row count");
    }

    // --- W26-REV-3: a dirty document must survive Recent/Blank Project Cancel.
    // Both handlers used to replace the document without the File > Open guard.
    const QString ini =
        QString::fromStdString((out_dir / "recent.ini").string());
    QFile::remove(ini);
    msqt::PreferencesStore store(ini);
    store.add_recent_file(QString::fromStdString(fixture.string()));
    window.set_preferences_store(&store);
    window.show_backstage(true);
    msqt::Backstage* backstage = window.backstage();
    check(backstage != nullptr, "the window has a backstage");
    if (backstage != nullptr) {
        backstage->show_page(msqt::Backstage::Page::create_new);
        QApplication::processEvents();
        QListWidget* recent = backstage->new_recent_list();
        check(recent != nullptr && recent->count() == 1,
              "the New page lists the local recent file");
        bool saw_recent_prompt = false;
        QTimer::singleShot(0, [&saw_recent_prompt]() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box != nullptr) {
                saw_recent_prompt = true;
                if (QAbstractButton* cancel = box->button(QMessageBox::Cancel)) {
                    cancel->click();
                } else {
                    box->reject();
                }
            }
        });
        if (recent != nullptr && recent->count() > 0) {
            recent->itemActivated(recent->item(0));
            QApplication::processEvents();
        }
        check(saw_recent_prompt, "a Recent row on a dirty document asks first");
        check(window.pending_start_edits() == 1 && window.document_dirty(),
              "Cancel on the Recent prompt keeps the pending Start edit");
        check(window.loaded_task_count() == 3,
              "Cancel on the Recent prompt keeps the open project");

        bool saw_blank_prompt = false;
        QTimer::singleShot(0, [&saw_blank_prompt]() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box != nullptr) {
                saw_blank_prompt = true;
                if (QAbstractButton* cancel = box->button(QMessageBox::Cancel)) {
                    cancel->click();
                } else {
                    box->reject();
                }
            }
        });
        if (QAbstractButton* blank = backstage->new_blank_button()) {
            blank->click();
            QApplication::processEvents();
        }
        check(saw_blank_prompt, "Blank Project on a dirty document asks first");
        check(window.pending_start_edits() == 1 && window.document_dirty(),
              "Cancel on the Blank Project prompt keeps the pending Start edit");
        check(window.loaded_task_count() == 3,
              "Cancel on the Blank Project prompt keeps the open project");
    }
    window.set_preferences_store(nullptr);

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
        check(window.sheet_row_count() == 3,
              "the saved file reopens with its three task rows");
        check(window.gantt_bar_count() == 3,
              "the saved file reopens with its three task bars");
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
