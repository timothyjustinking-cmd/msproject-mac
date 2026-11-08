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
//   qt_sort_persist_tests <wave35-viewstate-sort-name-desc-control.mpp>

#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "edit_model.h"
#include "entry_columns.h"
#include "layout_model.h"
#include "main_window.h"
#include "sheet_table_model.h"
#include "shell_style.h"
#include "sort.h"

#include "gate_util.h"

namespace {

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
    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_sort_persist_tests <fixture.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);

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

    std::printf("qt_sort_persist: ");
    return gate::finish();
}
