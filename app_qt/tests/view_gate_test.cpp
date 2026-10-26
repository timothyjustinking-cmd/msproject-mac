// app_qt/tests/view_gate_test.cpp -- wave 20's per-screen gate for the two
// non-task view families: the Resource Sheet and the two Usage grids.
//
// The 9/9 parity score measured only the Task Sheet + Gantt.  These screens had
// no gate at all, so a wrong column set, a lost 22 px row pitch or a Gantt left
// visible in a grid view could ship.  This test loads the committed fixture and
// asserts, from the live widget tree:
//
//   * the frozen 60 px gutter, the 42 px caption band, the 22 px row pitch of
//     the task and Resource Sheets (the usage sheets draw Project's 21 px
//     detail sub-rows, W31-4.4);
//   * the Resource Sheet's six-column set and its measured pixel widths (W31-3.6:
//     Project's chars * font unit + 3 rule, the stock 6 px unit when the file
//     stores no Resource Sheet view);
//   * Task Usage's 147 px label, 48 px Details column, 64 px day-period columns
//     and 84 px port Total column, and that Day/Week/Month re-phase the grid at
//     the measured 64 px period width (this fixture stores no usage view, so the
//     shell's own fallback presentation applies -- the stored presentation is
//     gated by qt_usage_gate on wave31-usage.mpp);
//   * Resource Usage's 123 px "Resource Name" label column;
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

void check_view_chrome(msqt::MainWindow& window, const char* what, int row_pitch) {
    QTableView* sheet = window.sheet_view();
    gate::check(sheet != nullptr, "the view owns a sheet view");
    if (sheet == nullptr) {
        return;
    }
    gate::check(sheet->verticalHeader() != nullptr && sheet->verticalHeader()->isVisible(),
                "the view keeps the frozen row-number gutter");
    if (sheet->verticalHeader() != nullptr) {
        gate::check(msqt::kViewLabelWidth + sheet->verticalHeader()->width() ==
                        msqt::kGutterWidth,
                    "the gutter is 60 px in every view (caption + numbers)");
        gate::check(sheet->verticalHeader()->defaultSectionSize() == row_pitch,
                    "the row pitch is the view's measured pitch");
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
    check_view_chrome(window, "resource sheet", msqt::kRowHeight);
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
    // W31-3.6: the fallback column list's stored character widths at Project's
    // measured unit (6 px/char + 3 px): 6/20/11/11/10/11 characters.
    check_widths(window, {39, 123, 69, 69, 63, 69});

    // --- Task Usage, day ---------------------------------------------------
    QAction* task_usage = action(window, app::MenuAction::view_task_usage);
    if (task_usage != nullptr) {
        task_usage->trigger();
        QApplication::processEvents();
    }
    gate::check(window.view_mode() == app::ViewMode::task_usage,
                "the Task Usage action switches the view");
    check_view_chrome(window, "task usage", msqt::kUsageRowHeight);
    QAbstractItemModel* usage = window.sheet_view()->model();
    gate::check(usage != nullptr && usage->columnCount() >= 3,
                "Task Usage has a label, Details and a Total column at least");
    if (usage != nullptr) {
        gate::check(title_of(usage, 0) == QStringLiteral("Task Name"),
                    "Task Usage's first column is Task Name");
        gate::check(title_of(usage, 1) == QStringLiteral("Details"),
                    "Task Usage's second column is Details");
        const int last = usage->columnCount() - 1;
        gate::check(title_of(usage, last) == QStringLiteral("Total"),
                    "Task Usage's last column is Total");
        for (int column = 2; column < last; ++column) {
            gate::check(!title_of(usage, column).isEmpty(),
                        "every Task Usage period column has a day label");
            gate::check(window.sheet_view()->columnWidth(column) == 64,
                        "a fallback day period column is the measured 64 px");
        }
    }
    gate::check(window.sheet_view()->columnWidth(0) == 147,
                "the usage label column is the measured 147 px");
    gate::check(usage != nullptr && window.sheet_view()->columnWidth(1) == 48,
                "the Details column is the measured 48 px");
    gate::check(usage != nullptr &&
                    window.sheet_view()->columnWidth(usage->columnCount() - 1) == 84,
                "the usage Total column keeps the port's 84 px");

    // --- the period switch -------------------------------------------------
    if (QAction* week = action(window, app::MenuAction::usage_week); week != nullptr) {
        week->trigger();
        QApplication::processEvents();
        QAbstractItemModel* week_model = window.sheet_view()->model();
        if (week_model != nullptr && week_model->columnCount() > 3) {
            gate::check(window.sheet_view()->columnWidth(0) == 147 &&
                            window.sheet_view()->columnWidth(1) == 48,
                        "the week grid keeps the measured label/Details widths");
            gate::check(window.sheet_view()->columnWidth(2) == 64,
                        "a fallback week period column keeps the 64 px minimum");
            gate::check(title_of(week_model, week_model->columnCount() - 1) ==
                            QStringLiteral("Total"),
                        "the week grid still ends with Total");
        }
    }
    if (QAction* month = action(window, app::MenuAction::usage_month); month != nullptr) {
        month->trigger();
        QApplication::processEvents();
        if (window.sheet_view()->model() != nullptr &&
            window.sheet_view()->model()->columnCount() > 3) {
            gate::check(window.sheet_view()->columnWidth(2) == 64,
                        "a fallback month period column keeps the 64 px minimum");
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
    check_view_chrome(window, "resource usage", msqt::kUsageRowHeight);
    QAbstractItemModel* resource_usage_model = window.sheet_view()->model();
    gate::check(resource_usage_model != nullptr &&
                    title_of(resource_usage_model, 0) == QStringLiteral("Resource Name"),
                "Resource Usage's first column is Resource Name");
    gate::check(resource_usage_model != nullptr &&
                    title_of(resource_usage_model, resource_usage_model->columnCount() - 1) ==
                        QStringLiteral("Total"),
                "Resource Usage ends with Total");
    if (resource_usage_model != nullptr) {
        gate::check(window.sheet_view()->columnWidth(0) == 123,
                    "Resource Usage's label column is the measured 123 px");
        gate::check(window.sheet_view()->columnWidth(1) == 48,
                    "Resource Usage's Details column is the measured 48 px");
        for (int column = 2; column < resource_usage_model->columnCount() - 1;
             ++column) {
            gate::check(window.sheet_view()->columnWidth(column) == 64,
                        "Resource Usage's fallback period column is 64 px");
        }
    }

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
