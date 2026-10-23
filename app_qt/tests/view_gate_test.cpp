// app_qt/tests/view_gate_test.cpp -- wave 20's per-screen gate for the two
// non-task view families: the Resource Sheet and the two Usage grids.
//
// The 9/9 parity score measured only the Task Sheet + Gantt.  These screens had
// no gate at all, so a wrong column set, a lost 22 px row pitch or a Gantt left
// visible in a grid view could ship.  This test loads the committed fixture and
// asserts, from the live widget tree:
//
//   * the frozen 60 px gutter, the 42 px caption band and the 22 px row pitch;
//   * the Resource Sheet's six-column set and its measured pixel widths
//     (the generic view rule: column width in characters * 7, clamped 50..400);
//   * Task Usage's Label column, its day-period columns and the Total column,
//     and that Day/Week/Month switch the period column width (98/126/84 px);
//   * Resource Usage's "Resource Name" label column;
//   * the Gantt/chart pane is hidden in every grid view.
#include <QAbstractItemModel>
#include <QApplication>
#include <QHeaderView>
#include <QTableView>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "gantt_canvas.h"
#include "main_window.h"
#include "menu_model.h"
#include "shell_style.h"

#include "gate_util.h"

namespace {

QString title_of(QAbstractItemModel* model, int column) {
    if (model == nullptr || column < 0) {
        return {};
    }
    return model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
}

void check_view_chrome(msqt::MainWindow& window, const char* what) {
    QTableView* sheet = window.sheet_view();
    gate::check(sheet != nullptr, "the view owns a sheet view");
    if (sheet == nullptr) {
        return;
    }
    gate::check(sheet->verticalHeader() != nullptr && sheet->verticalHeader()->isVisible(),
                "the view keeps the frozen row-number gutter");
    if (sheet->verticalHeader() != nullptr) {
        gate::check(sheet->verticalHeader()->width() == msqt::kGutterWidth,
                    "the gutter is 60 px in every view");
        gate::check(sheet->verticalHeader()->defaultSectionSize() == msqt::kRowHeight,
                    "the row pitch is 22 px in every view");
    }
    gate::check(sheet->horizontalHeader() != nullptr && sheet->horizontalHeader()->isVisible(),
                "the caption band exists in every view");
    if (sheet->horizontalHeader() != nullptr) {
        gate::check(sheet->horizontalHeader()->height() == msqt::kSheetHeaderHeight,
                    "the caption band is 42 px in every view");
    }
    gate::check(window.chart_panel() != nullptr && !window.chart_panel()->isVisible(),
                "the chart pane is hidden in a grid view");
    std::fprintf(stderr, "note: %s rows %d columns %d\n", what,
                 sheet->model() != nullptr ? sheet->model()->rowCount() : 0,
                 sheet->model() != nullptr ? sheet->model()->columnCount() : 0);
}

void check_widths(msqt::MainWindow& window, const std::vector<int>& expected) {
    QTableView* sheet = window.sheet_view();
    if (sheet == nullptr || sheet->model() == nullptr) {
        gate::check(false, "the view has a model");
        return;
    }
    gate::check(sheet->model()->columnCount() == static_cast<int>(expected.size()),
                "the view's column count matches the app model's column set");
    const int count = std::min<int>(sheet->model()->columnCount(),
                                    static_cast<int>(expected.size()));
    for (int column = 0; column < count; ++column) {
        if (sheet->columnWidth(column) != expected[static_cast<std::size_t>(column)]) {
            std::fprintf(stderr, "FAIL: column %d width %d, want %d\n", column,
                         sheet->columnWidth(column), expected[static_cast<std::size_t>(column)]);
            ++gate::failures;
        }
    }
}

QAction* action(msqt::MainWindow& window, app::MenuAction id) {
    QAction* found = gate::find_action(window, id);
    gate::check(found != nullptr, "the menu carries the view action");
    return found;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_view_gate <fixture.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();

    // --- Resource Sheet ----------------------------------------------------
    QAction* resource_sheet = action(window, app::MenuAction::view_resource_sheet);
    if (resource_sheet != nullptr) {
        resource_sheet->trigger();
        QApplication::processEvents();
    }
    gate::check(window.view_mode() == app::ViewMode::resource_sheet,
                "the Resource Sheet action switches the view");
    check_view_chrome(window, "resource sheet");
    QAbstractItemModel* resource_model = window.sheet_view()->model();
    gate::check(title_of(resource_model, 0) == QStringLiteral("ID"),
                "Resource Sheet column 1 is ID");
    gate::check(title_of(resource_model, 1) == QStringLiteral("Resource Name"),
                "Resource Sheet column 2 is Resource Name");
    gate::check(title_of(resource_model, 2) == QStringLiteral("Type"),
                "Resource Sheet column 3 is Type");
    gate::check(title_of(resource_model, 3) == QStringLiteral("Initials"),
                "Resource Sheet column 4 is Initials");
    gate::check(title_of(resource_model, 4) == QStringLiteral("Max. Units"),
                "Resource Sheet column 5 is Max. Units");
    gate::check(title_of(resource_model, 5) == QStringLiteral("Work"),
                "Resource Sheet column 6 is Work");
    check_widths(window, {50, 140, 77, 77, 70, 77});

    // --- Task Usage, day ---------------------------------------------------
    QAction* task_usage = action(window, app::MenuAction::view_task_usage);
    if (task_usage != nullptr) {
        task_usage->trigger();
        QApplication::processEvents();
    }
    gate::check(window.view_mode() == app::ViewMode::task_usage,
                "the Task Usage action switches the view");
    check_view_chrome(window, "task usage");
    QAbstractItemModel* usage = window.sheet_view()->model();
    gate::check(usage != nullptr && usage->columnCount() >= 2,
                "Task Usage has a label and a Total column at least");
    if (usage != nullptr) {
        gate::check(title_of(usage, 0) == QStringLiteral("Task Name"),
                    "Task Usage's first column is Task Name");
        const int last = usage->columnCount() - 1;
        gate::check(title_of(usage, last) == QStringLiteral("Total"),
                    "Task Usage's last column is Total");
        for (int column = 1; column < last; ++column) {
            gate::check(!title_of(usage, column).isEmpty(),
                        "every Task Usage period column has a date label");
            gate::check(window.sheet_view()->columnWidth(column) == 98,
                        "a day period column is 14 chars * 7 = 98 px");
        }
    }
    gate::check(window.sheet_view()->columnWidth(0) == 210,
                "the usage label column is 30 chars * 7 = 210 px");
    gate::check(usage != nullptr &&
                    window.sheet_view()->columnWidth(usage->columnCount() - 1) == 84,
                "the usage Total column is 12 chars * 7 = 84 px");

    // --- the period switch -------------------------------------------------
    if (QAction* week = action(window, app::MenuAction::usage_week); week != nullptr) {
        week->trigger();
        QApplication::processEvents();
        QAbstractItemModel* week_model = window.sheet_view()->model();
        if (week_model != nullptr && week_model->columnCount() > 2) {
            gate::check(window.sheet_view()->columnWidth(1) == 126,
                        "a week period column is 18 chars * 7 = 126 px");
            gate::check(title_of(week_model, week_model->columnCount() - 1) ==
                            QStringLiteral("Total"),
                        "the week grid still ends with Total");
        }
    }
    if (QAction* month = action(window, app::MenuAction::usage_month); month != nullptr) {
        month->trigger();
        QApplication::processEvents();
        if (window.sheet_view()->model() != nullptr &&
            window.sheet_view()->model()->columnCount() > 2) {
            gate::check(window.sheet_view()->columnWidth(1) == 84,
                        "a month period column is 12 chars * 7 = 84 px");
        }
    }

    // --- Resource Usage ----------------------------------------------------
    QAction* resource_usage = action(window, app::MenuAction::view_resource_usage);
    if (resource_usage != nullptr) {
        resource_usage->trigger();
        QApplication::processEvents();
    }
    gate::check(window.view_mode() == app::ViewMode::resource_usage,
                "the Resource Usage action switches the view");
    check_view_chrome(window, "resource usage");
    QAbstractItemModel* resource_usage_model = window.sheet_view()->model();
    gate::check(resource_usage_model != nullptr &&
                    title_of(resource_usage_model, 0) == QStringLiteral("Resource Name"),
                "Resource Usage's first column is Resource Name");
    gate::check(resource_usage_model != nullptr &&
                    title_of(resource_usage_model, resource_usage_model->columnCount() - 1) ==
                        QStringLiteral("Total"),
                "Resource Usage ends with Total");

    // --- back to the Task Sheet (the chart must come back) ------------------
    if (QAction* task_sheet = action(window, app::MenuAction::view_task_sheet);
        task_sheet != nullptr) {
        task_sheet->trigger();
        QApplication::processEvents();
    }
    gate::check(window.view_mode() == app::ViewMode::task_sheet,
                "the Task Sheet action returns to the measured view");
    gate::check(window.chart_panel() != nullptr && window.chart_panel()->isVisible(),
                "the chart pane returns with the Task Sheet");

    std::printf("qt_view_gate: ");
    return gate::finish();
}
