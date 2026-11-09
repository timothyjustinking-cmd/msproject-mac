// app_qt/tests/outline_persist_test.cpp -- the View > Outline menu's result through
// the shell (wave 35, W35-2.3).
//
// apply_outline_level() is what the Outline menu's actions do.  Where the shape is
// one Project's wave35-viewstate-outline pairs measured (Level 1 or 2 on a measured
// outline that hides something) it queues a show_level edit that File > Save
// writes, and the session collapse is applied either way; every other level stays
// a session collapse and the status line names the reason.  All Subtasks
// withdraws a queued level.  The edit model's own rules and the bytes are proven
// by app_edits_show_level_tests and mppwrite_roundtrip_viewstate_outline.
//
//   qt_outline_persist_tests <wave35-viewstate-outline-level1-control.mpp>
//                            <wave35-viewstate-outline-deep-level1-control.mpp>

#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "edit_model.h"
#include "entry_columns.h"
#include "main_window.h"
#include "sheet_table_model.h"
#include "shell_style.h"

#include "gate_util.h"

namespace {

const app::StructuralEdit* queued_level(const msqt::MainWindow& window) {
    const std::vector<app::StructuralEdit>& applied = window.pending_edits().applied();
    if (applied.size() != 1 || applied.front().kind != app::StructuralKind::show_level) {
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
    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_outline_persist_tests <five-task.mpp> <six-task.mpp>\n");
        return 2;
    }
    const QString five = QString::fromLocal8Bit(argv[1]);
    const QString six = QString::fromLocal8Bit(argv[2]);

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(five, &error), "the five-task fixture loads");
    window.show();
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "nothing is queued on open");
    const std::size_t full_rows = window.sheet_row_count();
    gate::check(full_rows >= 5, "the sheet shows every row on open");

    // Level 1 is a measured pair: it is queued, with the file's rows and levels.
    window.apply_outline_level(1);
    QApplication::processEvents();
    const app::StructuralEdit* level1 = queued_level(window);
    gate::check(level1 != nullptr, "Level 1 queues one show_level edit");
    if (level1 != nullptr) {
        gate::check(level1->show_level == 1, "the queued edit is Level 1");
        gate::check(level1->outline_uids == std::vector<std::uint32_t>({1, 2, 3, 4, 5}),
                    "the queued rows are the file's");
        gate::check(level1->outline_levels == std::vector<std::uint32_t>({0, 1, 1, 0, 0}),
                    "the queued levels are the file's");
    }
    gate::check(status_contains(window, "queued; File > Save writes it"),
                "the status line says the level is queued");

    // The session collapse is applied as well: the two children are hidden.
    gate::check(window.sheet_row_count() == full_rows - 2,
                "Level 1 hides the two children for the session");

    // All Subtasks withdraws the queued level and expands the session.
    window.apply_outline_level(0);
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "All Subtasks withdraws the queued level");
    gate::check(window.sheet_row_count() == full_rows, "All Subtasks shows every row again");
    gate::check(status_contains(window, "the queued level was withdrawn"),
                "the status line says the queued level was withdrawn");

    // W35b-3.2 (R35G2-6): expanding or collapsing a row by hand while a level is
    // queued would leave the file describing rows the user is no longer looking at;
    // the toggle is refused, with the reason, and the queued level and the rows stay.
    window.apply_outline_level(1);
    QApplication::processEvents();
    gate::check(queued_level(window) != nullptr, "Level 1 is queued again for the toggle case");
    const std::size_t level1_rows = window.sheet_row_count();
    window.toggle_outline_row(0);
    QApplication::processEvents();
    gate::check(queued_level(window) != nullptr && queued_level(window)->show_level == 1,
                "a hand toggle leaves the queued Level 1 in place");
    gate::check(window.sheet_row_count() == level1_rows,
                "a hand toggle with a level queued changes no row");
    gate::check(status_contains(window, "a Show Level is queued and File > Save would write it"),
                "a hand toggle with a level queued: the status line names the queued level");
    // With nothing queued the same toggle works (All Subtasks first withdraws it).
    window.apply_outline_level(0);
    QApplication::processEvents();
    window.toggle_outline_row(0);
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "a hand toggle queues nothing");
    gate::check(window.sheet_row_count() == full_rows - 2,
                "a hand toggle with nothing queued collapses the summary's two children");
    window.apply_outline_level(0);
    QApplication::processEvents();
    gate::check(window.sheet_row_count() == full_rows, "All Subtasks shows every row again (2)");

    // Level 2 on this outline hides nothing: session only, with the reason.
    window.apply_outline_level(2);
    QApplication::processEvents();
    gate::check(window.pending_edits().empty(), "Level 2 hides nothing: nothing is queued");
    gate::check(status_contains(window, "hides nothing on this outline"),
                "Level 2: the status line names the reason");
    gate::check(status_contains(window, "session only"),
                "Level 2: the status line says session only");

    // Levels beyond the measured ones stay session only, each with its reason.
    for (const int level : {3, 4, 9}) {
        window.apply_outline_level(level);
        QApplication::processEvents();
        const std::string label = "Level " + std::to_string(level);
        gate::check(window.pending_edits().empty(), (label + ": nothing is queued").c_str());
        gate::check(status_contains(window, ("Show Level " + std::to_string(level) +
                                             " is not implemented in this port: the measured "
                                             "Project pairs show Level 1 and Level 2")
                                                .c_str()),
                    (label + ": the status line names the reason").c_str());
        gate::check(status_contains(window, "session only"),
                    (label + ": the status line says session only").c_str());
    }

    // The six-task three-level outline: Level 2 replaces a queued Level 1.
    gate::check(window.open_file(six, &error), "the six-task fixture loads");
    QApplication::processEvents();
    window.apply_outline_level(1);
    QApplication::processEvents();
    const app::StructuralEdit* deep1 = queued_level(window);
    gate::check(deep1 != nullptr && deep1->show_level == 1, "Level 1 queues on the deep outline");
    window.apply_outline_level(2);
    QApplication::processEvents();
    const app::StructuralEdit* deep2 = queued_level(window);
    gate::check(deep2 != nullptr && deep2->show_level == 2,
                "Level 2 replaces the queued Level 1 (Show Level is exclusive)");

    // W35b-1.5 (RW35-R2): a grid edit that is not saved yet.  Show Level is built
    // from the session plan, which carries the rename; the builder refuses it,
    // nothing is queued, and the status line says to save or undo first.
    {
        gate::check(window.open_file(five, &error), "the five-task fixture reopens");
        QApplication::processEvents();
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        gate::check(name_column >= 0, "the Entry table has a Name column");
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(name_column >= 0 &&
                        model->setData(model->index(1, name_column),
                                       QStringLiteral("Renamed in the grid"), Qt::EditRole),
                    "a Name cell edit commits through the shell's model path");
        window.apply_outline_level(1);
        QApplication::processEvents();
        gate::check(window.pending_edits().empty(), "unsaved rename: Level 1 queues nothing");
        gate::check(status_contains(window, "Show Level is not implemented in this port while "
                                            "the plan carries unsaved edits"),
                    "unsaved rename: the status line names the unsaved edits");
        gate::check(status_contains(window, "save or undo the grid edits first"),
                    "unsaved rename: the status line says save or undo the grid edits first");
        gate::check(status_contains(window, "session only"),
                    "unsaved rename: the status line says session only");
    }

    std::printf("qt_outline_persist: ");
    return gate::finish();
}
