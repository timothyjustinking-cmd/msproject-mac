// app_qt/tests/scroll_to_task_gate_test.cpp -- wave 30 (W30-1.7).
//
// THE TARGET (wave 30 brief, G1): Task > Editing > Scroll to Task is a VIEW
// command.  Project scrolls the sheet to the selected task and pans the Gantt
// chart so its bar is in view; neither action writes the project.
//
// WHAT THIS GATE DOES (all through live widgets)
//   1. opens tests/data/wave23-outline.mpp (six tasks; the first starts
//      2026-12-11 and the fourth starts 2026-12-16, so the chart can prove a
//      pan) and finds the Scroll to Task button on the LIVE Task tab / Editing
//      group; the button is enabled and runs RibbonCommand::scroll_to_task;
//   2. clicks the sheet row of uid 6 and the button: the chart's timescale
//      origin becomes that task's start day (2026-12-11) and the bar sits at
//      x = 0;
//   3. clicks the row of uid 4 and the button again: the origin moves to
//      2026-12-16, its bar sits at x = 0 and the earlier bar is clipped left
//      -- the pan really followed the selection;
//   4. shrinks the window and uses taller (user-resizable) rows so the sheet
//      cannot show all six rows, scrolls the sheet away from the selected row
//      and clicks the button: the selected row is scrolled back INTO the
//      viewport;
//   5. proves the whole command is view-only: the selection is unchanged and
//      the document is not dirty after every click.
//
// Usage: qt_scroll_to_task_gate <fixture.mpp> [out-dir]
#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QHeaderView>
#include <QModelIndex>
#include <QTableView>
// Qt 6.5.3's qtestmouse.h trips MSVC /W4 C4701 (an uninitialised local in the
// inline mouse helper).  The gate builds with /WX, so scope the warning off for
// this third-party header only.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4701)
#endif
#include <QTest>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gate_util.h"
#include "gantt.h"
#include "gantt_canvas.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "shell_style.h"

namespace {

// The Scroll to Task button, found by walking the LIVE control inventory (never
// by object name or a private pointer).
QAbstractButton* scroll_to_task_button(msqt::MainWindow& window) {
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
    const QStringList groups = ribbon->group_titles();
    const int editing = groups.indexOf(QStringLiteral("Editing"));
    if (editing < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(editing); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(editing, control));
        if (button != nullptr && button->text() == QStringLiteral("Scroll\nto Task")) {
            return button;
        }
    }
    return nullptr;
}

int name_column(QAbstractItemModel* model) {
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() ==
            QStringLiteral("Task Name")) {
            return column;
        }
    }
    return -1;
}

// The displayed (model) row whose task uid is `uid`, or -1.
int display_row_for_uid(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    QAbstractItemModel* model = window.sheet_view()->model();
    if (sheet == nullptr || model == nullptr) {
        return -1;
    }
    for (int row = 0; row < model->rowCount(); ++row) {
        const std::size_t record = sheet->visible_row(static_cast<std::size_t>(row));
        if (record != core::sheet::kNoIndex && sheet->row_uid(record) == uid) {
            return row;
        }
    }
    return -1;
}

// Select `uid` with a real mouse click on its Task Name cell.
bool click_task(msqt::MainWindow& window, std::uint32_t uid) {
    QAbstractItemModel* model = window.sheet_view()->model();
    const int row = display_row_for_uid(window, uid);
    const int column = name_column(model);
    if (row < 0) {
        return false;
    }
    const QModelIndex index = model->index(row, column < 0 ? 0 : column);
    window.sheet_view()->scrollTo(index);
    QApplication::processEvents();
    const QRect rect = window.sheet_view()->visualRect(index);
    QTest::mousePress(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      rect.center());
    QTest::mouseRelease(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                        rect.center());
    QApplication::processEvents();
    return true;
}

// The uid the sheet model has selected, or 0.
std::uint32_t selected_uid(msqt::MainWindow& window) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return 0;
    }
    const std::vector<std::size_t> rows = sheet->selected_rows();
    if (rows.size() != 1) {
        return 0;
    }
    return sheet->row_uid(rows.front());
}

// The start day of the bar's first row, in the chart's own screen space.
const core::gantt::Bar* bar_for(msqt::MainWindow& window, std::uint32_t uid) {
    return window.gantt().bar(uid);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_scroll_to_task_gate <fixture.mpp> [out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, 800);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    core::sheet::SheetModel* sheet = window.sheet();
    gate::check(sheet != nullptr, "the fixture has a sheet");
    if (sheet == nullptr) {
        return gate::finish();
    }

    QAbstractButton* scroll = scroll_to_task_button(window);
    gate::check(scroll != nullptr, "the Task tab's Editing group carries Scroll to Task");
    gate::check(scroll != nullptr && scroll->isEnabled(),
                "Scroll to Task is live (W30-1.7)");
    if (scroll == nullptr) {
        return gate::finish();
    }

    // The live control runs its own command, not some other button's.
    {
        msqt::RibbonBar* ribbon = window.ribbon();
        const QStringList groups = ribbon->group_titles();
        const int editing = groups.indexOf(QStringLiteral("Editing"));
        bool found = false;
        for (int control = 0; control < ribbon->group_control_count(editing); ++control) {
            if (ribbon->group_control(editing, control) == scroll) {
                found = ribbon->group_control_command(editing, control) ==
                        msqt::RibbonCommand::scroll_to_task;
                break;
            }
        }
        gate::check(found, "the live button is wired to RibbonCommand::scroll_to_task");
    }

    // --- 2/3. the chart pans to the selected task --------------------------
    gate::check(click_task(window, 6), "uid 6 has a sheet row");
    gate::check(selected_uid(window) == 6, "clicking the row selects uid 6");
    scroll->click();
    QApplication::processEvents();
    QApplication::processEvents();
    const core::sched::date32 first_day = core::sched::from_ymd(2026, 12, 11);
    const core::sched::date32 second_day = core::sched::from_ymd(2026, 12, 16);
    gate::check(window.gantt().timescale.origin == first_day,
                "Scroll to Task pans the chart to uid 6's start day (2026-12-11)");
    const core::gantt::Bar* first_bar = bar_for(window, 6);
    gate::check(first_bar != nullptr, "uid 6 has a Gantt bar");
    gate::check(first_bar != nullptr && std::fabs(first_bar->rect.x) < 0.001,
                "uid 6's bar starts at the chart's left edge");
    gate::check(!window.document_dirty(),
                "panning the chart never marks the document for saving");

    gate::check(click_task(window, 4), "uid 4 has a sheet row");
    gate::check(selected_uid(window) == 4, "clicking the row selects uid 4");
    scroll->click();
    QApplication::processEvents();
    QApplication::processEvents();
    gate::check(window.gantt().timescale.origin == second_day,
                "Scroll to Task pans the chart to uid 4's start day (2026-12-16)");
    const core::gantt::Bar* second_bar = bar_for(window, 4);
    gate::check(second_bar != nullptr, "uid 4 has a Gantt bar");
    gate::check(second_bar != nullptr && std::fabs(second_bar->rect.x) < 0.001,
                "uid 4's bar starts at the chart's left edge");
    const core::gantt::Bar* earlier_bar = bar_for(window, 6);
    gate::check(earlier_bar != nullptr && earlier_bar->rect.x < -0.001,
                "the earlier bar is clipped left of the pinned origin");
    gate::check(selected_uid(window) == 4,
                "Scroll to Task leaves the selection untouched");
    gate::check(!window.document_dirty(),
                "moving the chart origin never marks the document for saving");

    // --- 4. the sheet scrolls the selected row back into view --------------
    // A short window plus the row height a user can drag cannot show all six
    // rows, so the selected row can really be out of the viewport before the
    // click.  (The window stops at its minimum height; the rows do the rest.)
    window.resize(msqt::kParityWidth, 340);
    QApplication::processEvents();
    QApplication::processEvents();
    QTableView* view = window.sheet_view();
    // 96 px is a row height the user can drag to; it keeps the setup true even
    // if a platform's minimum window height is taller than this gate's 340.
    for (int row_index = 0; row_index < view->model()->rowCount(); ++row_index) {
        view->setRowHeight(row_index, 96);
    }
    QApplication::processEvents();
    gate::check(view->viewport()->height() > 0, "the short window keeps a usable sheet");
    const int row = display_row_for_uid(window, 4);
    gate::check(row >= 0, "uid 4 still has a displayed row");
    if (row < 0) {
        return gate::finish();
    }
    const QModelIndex index =
        view->model()->index(row, std::max(0, name_column(view->model())));
    view->scrollToTop();
    QApplication::processEvents();
    const QRect before = view->visualRect(index);
    std::fprintf(stderr,
                 "note: window %dx%d, sheet viewport %dx%d, row rect y %d h %d\n",
                 window.width(), window.height(), view->viewport()->width(),
                 view->viewport()->height(), before.y(), before.height());
    gate::check(!view->viewport()->rect().contains(before),
                "the selected row is out of the viewport before Scroll to Task");
    scroll->click();
    QApplication::processEvents();
    QApplication::processEvents();
    const QRect after = view->visualRect(index);
    gate::check(view->viewport()->rect().contains(after),
                "Scroll to Task scrolls the selected row back into the viewport");
    gate::check(selected_uid(window) == 4, "the sheet scroll keeps the selection");
    gate::check(!window.document_dirty(),
                "scrolling the sheet never marks the document for saving");

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(window.grab().save(
                        out_dir + QStringLiteral("/scroll-to-task.png"), "PNG"),
                    "the short-window view is written for human review");
    }

    return gate::finish();
}
