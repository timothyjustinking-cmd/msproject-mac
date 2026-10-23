// app_qt/tests/shell_test.cpp -- the headless (offscreen) gate for wave 16 Q1.
//
// Runs the real MainWindow (not a mock) against the committed fixture
// tests/data/synthetic-base.mpp, which is read-only here: this test never saves.
// It asserts the three things the brief names:
//
//   * the fixture loads and shows its 3 task rows (wave 24: the file stores the
//     project-summary row OFF, so there is no row 0) and the matching 3 bars;
//   * the QMenuBar's command list is app::menu_specs() -- same menus, same
//     command order, same titles, same MenuAction ids;
//   * the window renders 900x560 and the PNG it writes reads back at 900x560.
//
// The CLI's own --render path is covered by the qt_render_smoke and
// qt_render_document CTest entries, which use the same fixed size.
//
// Run it with QT_QPA_PLATFORM=offscreen (app_qt/CMakeLists.txt does).
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPalette>
#include <QMenu>
#include <QMenuBar>
#include <QModelIndex>
#include <QPixmap>
#include <QString>
#include <QTableView>

#include <cstddef>
#include <cstdio>
#include <vector>

#include "gantt_canvas.h"
#include "main_window.h"
#include "menu_model.h"
#include "shell_style.h"
#include "timeline_strip.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // The same Fusion style, palette and font the executable uses.
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_shell_tests <fixture.mpp> <out.png>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString output = QString::fromLocal8Bit(argv[2]);

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);

    QString error;
    if (!window.open_file(fixture, &error)) {
        const QByteArray utf8 = error.toUtf8();
        std::fprintf(stderr, "FAIL: could not open %s: %s\n", argv[1], utf8.constData());
        return 1;
    }

    // --- the loaded plan ---------------------------------------------------
    // Wave 24 (W24-2): the file stores 214/Props 0x03400009 = 0, so Project
    // draws no project-summary row for it; the sheet is the three tasks.
    check(window.sheet_row_count() == 3, "the fixture shows its three task rows");
    check(window.gantt_bar_count() == 3, "the fixture has three task bars");
    // Wave 24 (W24-3): the file stores 214/Props 0x03400008 = "Time&line", so
    // Project shows the Timeline strip for this fixture; the port follows the
    // file rather than an application default.
    check(window.timeline() != nullptr && !window.timeline()->isHidden(),
          "the loaded fixture shows the Timeline strip (the file stores it shown)");
    // --- the same-file structural gate (wave 23, A-1a) ---------------------
    // Literals typed from the clean capture / the measured reference set, never
    // production symbols: "Ready" at the left, selection #A2D3A0, stock bar
    // colours (black summary because the file's styles are not decoded, Q1).
    check(window.status_left_text() == QStringLiteral("Ready"),
          "the status bar's left segment says Ready");
    // Wave 23 (B-10): "<file name>.mpp - <port name>", not title + full path.
    check(window.windowTitle().endsWith(QStringLiteral(" - MS Project Shell")),
          "the window title names the port after the file");
    check(!window.windowTitle().contains(QStringLiteral("/")) &&
              !window.windowTitle().contains(QStringLiteral("\\")),
          "the window title carries the file name, not its path");
    if (window.sheet_view() != nullptr) {
        check(window.sheet_view()->palette().color(QPalette::Highlight) ==
                  QColor(0xa2, 0xd3, 0xa0),
              "the selection colour is Project's measured #A2D3A0");
        check(window.sheet_view()->font().family() == QApplication::font().family() &&
                  !window.sheet_view()->font().family().isEmpty(),
              "the sheet's cells are drawn in the resolved UI font family");
    }
    if (window.gantt_view() != nullptr) {
        check(window.gantt_view()->layout().bars.size() == 3,
              "the Gantt has the three task bars");
        if (window.gantt_view()->layout().bars.size() == 3) {
            // Wave 25 (W25-2/S2): synthetic-base's tasks are MANUALLY scheduled
            // (it was built with Tasks.Add and no Manual = False), so the manual
            // task style is the measured teal; an auto task is (138,187,237).
            check(window.gantt_view()->bar_colour(0) == QColor(0x74, 0xcc, 0xd4),
                  "a manually scheduled task bar uses Project's measured teal (W25-2)");
        }
    } else {
        check(false, "the window owns a Gantt canvas");
    }
    check(window.sheet_view() != nullptr, "the window owns a sheet view");
    if (window.sheet_view() != nullptr && window.sheet_view()->model() != nullptr) {
        check(window.sheet_view()->model()->rowCount() == 3,
              "the table model's row count is the three tasks (no project row)");
        check(window.sheet_view()->model()->columnCount() == 9,
              "the table model shows the 9 Entry columns (the gutter is the frozen row "
              "header, wave 18 Q3)");
        // Wave 24 (W24-2): this file hides the project row, so row 0 is a real
        // task and every row is editable.  Wave 25 (W25-11/S14): the project
        // row's own refusal is pinned in qt_uid_routing (a file that stores the
        // row shown), not here, in qt_task_order_corpus or in save_reopen
        // (both use files that hide it).
        const int name_column =
            window.sheet_table_model()->display_column_for_field(app::name_field_id());
        const QModelIndex task_cell = window.sheet_view()->model()->index(0, name_column);
        check(window.sheet_view()->model()->flags(task_cell).testFlag(Qt::ItemIsEditable),
              "a task row is editable");
    } else {
        check(false, "the sheet view has a model");
    }

    // --- wave 24 (W24-3): the strip is FILE state ---------------------------
    // A second fixture is Project's save of the same plan with the Timeline
    // hidden (214/Props 0x03400008 empty).  Its partner synthetic-base stores
    // "Time&line" and showed the strip above; the port must follow each file.
    if (argc > 3) {
        msqt::MainWindow hidden_window;
        hidden_window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
        QString hidden_error;
        if (!hidden_window.open_file(QString::fromLocal8Bit(argv[3]), &hidden_error)) {
            std::fprintf(stderr, "FAIL: could not open %s: %s\n", argv[3],
                         hidden_error.toUtf8().constData());
            return 1;
        }
        check(hidden_window.sheet_row_count() == 3,
              "the Timeline-hidden fixture still shows three task rows");
        check(hidden_window.timeline() != nullptr && hidden_window.timeline()->isHidden(),
              "the Timeline-hidden fixture hides the strip (the file stores it hidden)");
    }

    // --- the menu bar is app::menu_model -----------------------------------
    const std::vector<app::MenuSpec>& specs = app::menu_specs();
    QMenuBar* bar = window.menuBar();
    check(bar != nullptr, "the window has a menu bar");
    if (bar != nullptr) {
        check(static_cast<std::size_t>(bar->actions().size()) == specs.size(),
              "the menu count matches app::menu_specs()");

        std::vector<int> expected_ids;
        std::vector<int> actual_ids;
        std::vector<QString> expected_titles;
        std::vector<QString> actual_titles;

        for (std::size_t menu_index = 0; menu_index < specs.size(); ++menu_index) {
            if (menu_index >= static_cast<std::size_t>(bar->actions().size())) {
                break;
            }
            QMenu* menu = bar->actions()[static_cast<int>(menu_index)]->menu();
            check(menu != nullptr, "every menu-bar entry owns a menu");
            if (menu == nullptr) {
                continue;
            }
            // The macOS global menu bar owns the application menu's title (a
            // documented chrome exception, docs/ui-qt-port.md section 2.7).
#ifndef Q_OS_MACOS
            check(menu->title() == QString::fromUtf8(specs[menu_index].title),
                  "a menu's title matches app::menu_specs()");
#endif

            // Our commands are the ones carrying a MenuAction in QAction::data().
            // A platform may add its own item to a menu (the macOS Help search);
            // that is documented chrome, not a shell command, so it is skipped.
            std::vector<QAction*> commands;
            for (QAction* action : menu->actions()) {
                if (action->isSeparator() || !action->data().isValid()) {
                    continue;
                }
                commands.push_back(action);
            }
            check(commands.size() == specs[menu_index].items.size(),
                  "a menu lists every modelled command and no other");
            const std::size_t count =
                commands.size() < specs[menu_index].items.size()
                    ? commands.size()
                    : specs[menu_index].items.size();
            for (std::size_t action_index = 0; action_index < count; ++action_index) {
                const app::MenuItemSpec& item = specs[menu_index].items[action_index];
                QAction* action = commands[action_index];
                expected_ids.push_back(static_cast<int>(item.action));
                actual_ids.push_back(action->data().toInt());
                expected_titles.push_back(QString::fromUtf8(item.title));
                actual_titles.push_back(action->text());
            }
        }
        check(expected_ids == actual_ids, "the command ids match app::menu_specs()");
        check(expected_titles == actual_titles, "the command titles match app::menu_specs()");
    }

    // --- the render --------------------------------------------------------
    window.show();
    QApplication::processEvents();
    const QPixmap shot = window.grab();
    check(!shot.isNull(), "the window grab is not null");
    check(shot.width() == msqt::kWindowWidth, "the window grab is 900 px wide");
    check(shot.height() == msqt::kWindowHeight, "the window grab is 560 px high");
    check(shot.save(output, "PNG"), "the window grab is written as a PNG");

    const QImage image(output);
    check(!image.isNull(), "the rendered PNG reads back");
    check(image.width() == msqt::kWindowWidth, "the rendered PNG is 900 px wide");
    check(image.height() == msqt::kWindowHeight, "the rendered PNG is 560 px high");

    std::printf("qt_shell_offscreen: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
