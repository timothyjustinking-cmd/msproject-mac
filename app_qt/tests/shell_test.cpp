// app_qt/tests/shell_test.cpp -- the headless (offscreen) gate for wave 16 Q1.
//
// Runs the real MainWindow (not a mock) against the committed fixture
// tests/data/synthetic-base.mpp, which is read-only here: this test never saves.
// It asserts the three things the brief names:
//
//   * the fixture loads and shows the project row plus its 3 task rows (and
//     the matching 4 Gantt bars, wave 23 B-1);
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
    // Wave 23 (B-1): the sheet shows Project's project-summary row (ID 0) ahead
    // of the fixture's three tasks, and the Gantt draws its project bar.
    check(window.sheet_row_count() == 4, "the fixture shows the project row + 3 task rows");
    check(window.gantt_bar_count() == 4, "the fixture has the project bar + 3 task bars");
    // Wave 23 (B-2, corrected): Project shows the Timeline strip for this
    // fixture (measured in its 1920 capture) even though COM reports the same
    // "Gantt Chart" view as project_a.  The strip is application state, so the
    // shell keeps Project's default and lays it out (isHidden false; the
    // offscreen window is never shown itself).
    check(window.timeline() != nullptr && !window.timeline()->isHidden(),
          "the loaded fixture shows the Timeline strip (Project's default)");
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
        check(window.gantt_view()->layout().bars.size() == 4,
              "the Gantt has the project bar and three task bars");
        if (window.gantt_view()->layout().bars.size() == 4) {
            check(window.gantt_view()->bar_colour(0) == QColor(0x00, 0x00, 0x00),
                  "the project-summary bar uses Project's stock black (Q1)");
            check(window.gantt_view()->bar_colour(1) == QColor(0x4a, 0x7e, 0xbb),
                  "an ordinary task bar uses the stock blue");
        }
    } else {
        check(false, "the window owns a Gantt canvas");
    }
    check(window.sheet_view() != nullptr, "the window owns a sheet view");
    if (window.sheet_view() != nullptr && window.sheet_view()->model() != nullptr) {
        check(window.sheet_view()->model()->rowCount() == 4,
              "the table model's row count is the project row + 3 tasks");
        check(window.sheet_view()->model()->columnCount() == 9,
              "the table model shows the 9 Entry columns (the gutter is the frozen row "
              "header, wave 18 Q3)");
    } else {
        check(false, "the sheet view has a model");
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
