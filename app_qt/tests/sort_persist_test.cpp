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

#include <QApplication>
#include <QString>

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
#include "edit_model.h"
#include "entry_columns.h"
#include "fixture_guard.h"
#include "layout_model.h"
#include "main_window.h"
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

bool status_contains(const msqt::MainWindow& window, const char* needle) {
    return window.status_left_text().contains(QString::fromUtf8(needle));
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

    // W35b-1.5 (RW35-R2): a grid edit that is not saved yet.  The session plan
    // carries the rename, so Sort would persist an order of names the file does
    // not store; the builder refuses it, nothing is queued, and the status line
    // says to save or undo the grid edits first.
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
        gate::check(status_contains(window, "save or undo the grid edits first"),
                    "unsaved rename: the status line says save or undo the grid edits first");
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
