// app_qt/tests/chrome_gate_test.cpp -- wave 20's structural gate for the LOADED
// project's chrome, the parts the 9/9 sheet score did not cover:
//
//   * the open document's name is in the title bar;
//   * the row-number gutter actually paints numbers for the visible rows;
//   * the quick-access toolbar carries the shared Save/Undo/Redo actions;
//   * the status bar's four view buttons carry icons and labels, and the zoom
//     group has minus / slider / plus (no percentage);
//   * the rotated "GANTT CHART" caption is visible in the chart pane;
//   * the new-project callout is hidden once a document is open.
//
// Presence/position/size only; no reference image is stored or compared.
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QStringList>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QSlider>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <cstdio>
#include <string>
#include <vector>

#include "gantt_canvas.h"
#include "main_window.h"
#include "sheet_views.h"
#include "shell_style.h"
#include "view_label.h"

#include "gate_util.h"

namespace {

bool gutter_has_number(const QImage& image, const QPoint& header_origin, int row, int gutter) {
    const int top = header_origin.y() + row * msqt::kRowHeight + 4;
    const int bottom = header_origin.y() + (row + 1) * msqt::kRowHeight - 3;
    for (int y = top; y < bottom; ++y) {
        for (int x = header_origin.x() + 2; x < header_origin.x() + gutter - 2; ++x) {
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
                continue;
            }
            const QColor colour = image.pixelColor(x, y);
            if (colour.red() < 160 && colour.green() < 160 && colour.blue() < 160) {
                return true;
            }
        }
    }
    return false;
}

QStringList action_labels(QMenu* menu) {
    QStringList labels;
    if (menu == nullptr) {
        return labels;
    }
    for (QAction* action : menu->actions()) {
        if (action != nullptr && !action->isSeparator() && !action->text().isEmpty()) {
            labels.push_back(action->text());
        }
    }
    return labels;
}

QAction* action_named(QMenu* menu, const QString& text) {
    if (menu == nullptr) {
        return nullptr;
    }
    for (QAction* action : menu->actions()) {
        if (action != nullptr && action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

void check_menus(msqt::MainWindow& window) {
    QTableView* sheet = window.sheet_view();
    gate::check(sheet != nullptr, "the loaded window has a sheet for the menus");
    if (sheet == nullptr) {
        return;
    }
    auto* header = static_cast<msqt::SheetHeaderView*>(sheet->horizontalHeader());
    gate::check(header != nullptr, "the sheet header is our SheetHeaderView");
    if (header != nullptr) {
        QMenu* column = header->build_column_menu(3);
        gate::check(column != nullptr, "the column header menu builds");
        if (column != nullptr) {
            const QStringList expected = {
                QStringLiteral("Cut"),
                QStringLiteral("Copy"),
                QStringLiteral("Paste"),
                QStringLiteral("Paste Special..."),
                QStringLiteral("Wrap Text"),
                QStringLiteral("Insert Column"),
                QStringLiteral("Hide Column"),
                QStringLiteral("Font..."),
                QStringLiteral("Text Styles..."),
                QStringLiteral("Field Settings"),
                QStringLiteral("Custom Fields"),
                QStringLiteral("Data Type")};
            gate::check(action_labels(column) == expected,
                        "the column menu has the measured command order");
            QComboBox* family = column->findChild<QComboBox*>(
                QStringLiteral("columnFontFamily"));
            gate::check(family != nullptr && family->currentText() == QStringLiteral("Arial"),
                        "the column menu carries the Arial mini format bar");
            gate::check(column->findChildren<QToolButton*>(
                            QStringLiteral("columnFormatButton")).size() == 3,
                        "the mini format bar has B/I/U");
            QAction* special = action_named(column, QStringLiteral("Paste Special..."));
            gate::check(special != nullptr && !special->isEnabled(),
                        "an unimplemented column command is disabled");
            delete column;
        }
    }

    QMenu* filter = window.build_filter_menu();
    gate::check(filter != nullptr, "the filter drop-down builds");
    if (filter != nullptr) {
        const QStringList expected = {
            QStringLiteral("Sort Earliest to Latest"),
            QStringLiteral("Sort Latest to Earliest"),
            QStringLiteral("Group by: No Group"),
            QStringLiteral("Clear Filter from Start"),
            QStringLiteral("Filters"),
            QStringLiteral("Select All")};
        gate::check(action_labels(filter) == expected,
                    "the filter menu has the measured sort/group/filter order");
        QTreeWidget* tree =
            filter->findChild<QTreeWidget*>(QStringLiteral("filterDateTree"));
        gate::check(tree != nullptr, "the filter menu has the checkable date tree");
        if (tree != nullptr && tree->topLevelItemCount() == 1) {
            QTreeWidgetItem* year = tree->topLevelItem(0);
            gate::check(year->text(0) == QString::number(QDate::currentDate().year()),
                        "the date tree starts at the current year");
            gate::check(year->childCount() == 12, "the date tree has twelve months");
            if (year->childCount() > 0) {
                QTreeWidgetItem* january = year->child(0);
                gate::check(january->text(0) == QStringLiteral("January"),
                            "the first month is January");
                if (january->childCount() > 0) {
                    QTreeWidgetItem* day = january->child(0);
                    gate::check(day->text(0) == QStringLiteral("1"), "the first day is 1");
                    gate::check((day->flags() & Qt::ItemIsUserCheckable) != 0,
                                "a day row is checkable");
                }
            }
        }
        QPushButton* cancel =
            filter->findChild<QPushButton*>(QStringLiteral("filterDateCancel"));
        gate::check(cancel != nullptr, "the filter date panel has Cancel");
        if (cancel != nullptr) {
            filter->show();
            QApplication::processEvents();
            cancel->click();
            QApplication::processEvents();
            gate::check(!filter->isVisible(), "Cancel closes the filter drop-down");
        }
        delete filter;
    }

    QMenu* row = window.build_row_menu();
    gate::check(row != nullptr, "the task-row context menu builds");
    if (row != nullptr) {
        const QStringList labels = action_labels(row);
        gate::check(labels.size() == 16, "the row menu has its measured item count");
        if (labels.size() == 16) {
            gate::check(labels[0] == QStringLiteral("Cut") &&
                            labels[1] == QStringLiteral("Copy") &&
                            labels[2] == QStringLiteral("Paste"),
                        "the row menu starts with Cut/Copy/Paste");
            gate::check(labels[8].contains(QStringLiteral("Schedule")),
                        "the row menu reflects the current scheduling mode");
            gate::check(labels[15] == QStringLiteral("Link"),
                        "the row menu ends with Link");
        }
        QAction* special = action_named(row, QStringLiteral("Paste Special..."));
        gate::check(special != nullptr && !special->isEnabled(),
                    "an unimplemented row command is disabled");
        delete row;
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_chrome_gate <fixture.mpp> [out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
    }

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    // --- identity ----------------------------------------------------------
    const QString file_name = QFileInfo(fixture).fileName();
    gate::check(window.windowTitle().contains(file_name),
                "the title bar carries the open document's name");

    // --- the gutter paints row numbers ------------------------------------
    QTableView* sheet = window.sheet_view();
    gate::check(sheet != nullptr, "the loaded window has a sheet view");
    if (sheet != nullptr && sheet->verticalHeader() != nullptr) {
        gate::check(msqt::kViewLabelWidth + sheet->verticalHeader()->width() ==
                        msqt::kGutterWidth,
                    "the frozen gutter is 60 px (22 px caption + 38 px numbers)");
        // Wave 24 (W24-2): this fixture stores the project-summary row OFF.
        gate::check(sheet->model() != nullptr && sheet->model()->rowCount() == 3,
                    "the fixture shows its three task rows (no project row)");
        const QImage image = window.grab().toImage();
        gate::check(image.size() == QSize(window.width(), window.height()),
                    "the chrome grab is 1:1");
        const QPoint header_origin = sheet->verticalHeader()->mapTo(&window, QPoint(0, 0));
        for (int row = 0; row < (sheet->model() != nullptr ? sheet->model()->rowCount() : 0);
             ++row) {
            gate::check(gutter_has_number(image, header_origin, row,
                                          msqt::kGutterWidth - msqt::kViewLabelWidth),
                        "each visible row paints its number in the gutter");
        }
    }

    // --- quick-access toolbar ---------------------------------------------
    QToolBar* quick = window.quick_access();
    gate::check(quick != nullptr && quick->isVisible(), "the quick-access toolbar is visible");
    if (quick != nullptr) {
        gate::check(quick->actions().size() == 3,
                    "the quick-access toolbar carries Save/Undo/Redo");
        for (QAction* action : quick->actions()) {
            gate::check(action != nullptr && !action->icon().isNull(),
                        "a quick-access action has an icon");
        }
    }

    // --- status bar --------------------------------------------------------
    const std::vector<QToolButton*>& buttons = window.view_buttons();
    // Wave 23 (B-9): Project's five buttons, in its order; Team Planner and
    // Report are disabled with the decision-9 tooltip until their views exist.
    gate::check(buttons.size() == 5, "five status-bar view buttons (Project's order)");
    const char* expected_labels[] = {"Gantt Chart", "Task Usage", "Team Planner",
                                     "Resource Sheet", "Report"};
    for (std::size_t index = 0; index < buttons.size() && index < 5; ++index) {
        QToolButton* button = buttons[index];
        gate::check(button != nullptr && button->isVisible() && !button->icon().isNull(),
                    "a view button is visible and carries an icon");
        if (button != nullptr) {
            gate::check(button->text() == QString::fromLatin1(expected_labels[index]),
                        "the view buttons are in Project's order");
        }
    }
    if (buttons.size() == 5) {
        gate::check(!buttons[2]->isEnabled() && !buttons[4]->isEnabled(),
                    "Team Planner and Report are disabled (views not built)");
        gate::check(buttons[2]->toolTip() ==
                        QStringLiteral("not implemented in this port"),
                    "a missing view says so with the exact tooltip");
    }
    QSlider* zoom = window.zoom_slider();
    gate::check(zoom != nullptr && zoom->isVisible(), "the zoom slider is visible");
    gate::check(window.status_mode_label() != nullptr &&
                    window.status_mode_label()->text().contains(QStringLiteral("New Tasks")),
                "the status-bar centre shows the scheduling mode");

    // --- chart chrome and the callout's loaded-project state ---------------
    msqt::ViewLabel* label = window.view_label();
    gate::check(label != nullptr && label->isVisible(), "the rotated view caption is visible");
    if (label != nullptr) {
        gate::check(label->width() == msqt::kViewLabelWidth,
                    "the rotated view caption is 22 px wide");
        gate::check(!label->text().isEmpty(), "the rotated view caption names the view");
    }
    QLabel* callout = window.info_callout();
    gate::check(callout != nullptr && !callout->isVisible(),
                "the new-project callout is hidden once a document is open");

    check_menus(window);

    if (!out_dir.isEmpty()) {
        const QPixmap shot = window.grab();
        gate::check(shot.save(out_dir + QStringLiteral("/qt-chrome-loaded.png"), "PNG"),
                    "the loaded chrome render is written");
    }

    std::printf("qt_chrome_gate: ");
    return gate::finish();
}
