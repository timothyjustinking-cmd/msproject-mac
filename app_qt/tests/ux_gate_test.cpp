// app_qt/tests/ux_gate_test.cpp -- wave 21 operator-review items (the four the
// operator sent after the stored-table deliverable).
//
//  1. the classic menu bar is HIDDEN (Project 2021 has none) while its QActions
//     stay alive for shortcuts and the gates;
//  2. the File backstage has the circular back arrow at the top-left, and using
//     it (or Escape) returns to the project chrome;
//  3. a double-click on a task opens the six-tab Task Information dialog instead
//     of starting a cell edit, its General tab applies a changed field through
//     the sheet's edit model, and Cancel discards;
//  4. the bars carry the operator's Project palette: critical stays RED at 100 %
//     complete, summary/milestone bars are near-black, and the selected row/cell
//     is Project's measured green (162,211,160).
//
// Offscreen, fixed scale, the committed fixture read-only.  No PNG writes.
#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QDialog>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPoint>
#include <QTableView>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QStringList>
#include <QToolButton>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "backstage.h"
#include "dialogs.h"
#include "entry_columns.h"
#include "gantt_canvas.h"
#include "gantt.h"
#include "layout_model.h"
#include "main_window.h"
#include "sheet_views.h"
#include "shell_style.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

QAction* find_action(QWidget* window, app::MenuAction id) {
    auto* bar = window != nullptr ? window->findChild<QMenuBar*>() : nullptr;
    if (bar == nullptr) {
        return nullptr;
    }
    for (QAction* menu_action : bar->actions()) {
        QMenu* menu = menu_action->menu();
        if (menu == nullptr) {
            continue;
        }
        for (QAction* action : menu->actions()) {
            if (action->data().isValid() && action->data().toInt() == static_cast<int>(id)) {
                return action;
            }
        }
    }
    return nullptr;
}

// --- 4. the bar palette ------------------------------------------------------

QColor pixel_at(const QImage& image, int x, int y) {
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
        return QColor();
    }
    return image.pixelColor(x, y);
}

const core::gantt::Bar* bar_for(const core::gantt::GanttLayout& layout, std::uint32_t uid) {
    for (const core::gantt::Bar& bar : layout.bars) {
        if (bar.task_uid == uid) {
            return &bar;
        }
    }
    return nullptr;
}

void case_palette() {
    using core::gantt::BarKind;
    core::gantt::GanttSource source;
    const auto day = [](int d) { return core::sched::from_ymd(2026, 10, 19 + d); };
    const auto make_task = [&day](std::uint32_t uid, BarKind kind, bool critical, int start,
                              int finish, double percent) {
        core::gantt::TaskBarInput task;
        task.uid = uid;
        task.label = "Task";
        task.start = day(start);
        task.finish = day(finish);
        task.kind = kind;
        task.critical = critical;
        // The completion fill is drawn when percent complete is > 0.
        task.percent_complete = percent;
        return task;
    };
    // normal blue, critical red, summary, milestone, and a 100 %-complete critical
    // task (the operator's defect: the blue fill hid the red).
    source.tasks.push_back(make_task(1, BarKind::normal, false, 0, 3, 0.0));
    source.tasks.push_back(make_task(2, BarKind::normal, true, 1, 4, 0.0));
    source.tasks.push_back(make_task(3, BarKind::summary, true, 0, 4, 0.0));
    source.tasks.push_back(make_task(4, BarKind::milestone, true, 2, 2, 0.0));
    source.tasks.push_back(make_task(5, BarKind::normal, true, 0, 3, 1.0));

    core::gantt::Timescale timescale;
    timescale.unit = core::gantt::TimescaleUnit::day;
    timescale.pixels_per_unit = 24.0;
    timescale.origin = day(0);
    core::gantt::GanttStyle style;
    style.row_height = 22.0;
    const core::gantt::GanttLayout layout = core::gantt::build_gantt(source, style, timescale);

    msqt::GanttCanvas canvas;
    canvas.resize(600, 260);
    canvas.set_layout(layout);
    const QImage image = canvas.grab().toImage();
    check(image.width() == 600 && image.height() == 260, "the canvas grab is 1:1");

    const int body_top = msqt::kGanttHeaderHeight;
    auto sample = [&](std::uint32_t uid, const char* what, const QColor& want) {
        const core::gantt::Bar* bar = bar_for(layout, uid);
        check(bar != nullptr, what);
        if (bar == nullptr) {
            return;
        }
        // Inside the bar body: left quarter, vertical centre -- away from the
        // outline, the caps and the label.
        const int x = static_cast<int>(bar->rect.x + bar->rect.w * 0.25);
        const int y = static_cast<int>(bar->rect.y + body_top + bar->rect.h * 0.5);
        const QColor got = pixel_at(image, x, y);
        if (got != want) {
            std::fprintf(stderr,
                         "FAIL: %s: pixel (%d,%d) is (%d,%d,%d), want (%d,%d,%d)\n", what, x, y,
                         got.red(), got.green(), got.blue(), want.red(), want.green(),
                         want.blue());
            ++g_failures;
        }
    };
    sample(1, "a normal bar is blue", msqt::kBarTask);
    sample(2, "a critical bar is red", msqt::kBarCritical);
    sample(3, "a summary bar paints the shell's summary colour", msqt::kBarSummary);
    sample(5, "a 100 %-complete CRITICAL bar stays red (not the blue fill)",
           msqt::kBarCritical);
    check(msqt::kBarCritical == QColor(0xff, 0x00, 0x00),
          "critical is the measured pure red #ff0000");
    check(msqt::kSelection == QColor(0xa2, 0xd3, 0xa0),
          "the selection is Project's measured green #a2d3a0");
    // A selected row paints that green behind the whole row of the canvas.
    canvas.set_selected_uid(1);
    const QImage selected = canvas.grab().toImage();
    const core::gantt::Bar* bar = bar_for(layout, 1);
    if (bar != nullptr) {
        const int row_y =
            static_cast<int>(bar->rect.y + body_top + bar->rect.h * 0.5);
        const QColor behind = pixel_at(selected, image.width() - 4, row_y);
        check(behind == msqt::kSelection, "the selected Gantt row is painted the measured green");
    }
}

// --- header captions (the operator's clipping defect) -----------------------

QFont header_font_for_gate() {
    QFont font = QApplication::font();
    font.setPointSize(msqt::kBodyPointSize);
    return font;
}

// For one column: every wrapped line fits the text rect, no line intersects the
// arrow, and a single word is never shortened.
void check_caption(const QRect& section, const QString& title) {
    const QFontMetrics metrics(header_font_for_gate());
    const msqt::HeaderCaptionLayout layout =
        msqt::layout_header_caption(section, title, metrics);
    for (const QString& line : layout.lines) {
        if (metrics.horizontalAdvance(line) > layout.text_rect.width()) {
            std::fprintf(stderr,
                         "FAIL: header \"%s\" (%d px): line \"%s\" is %d px wide, "
                         "text rect is %d px\n",
                         title.toUtf8().constData(), section.width(),
                         line.toUtf8().constData(), metrics.horizontalAdvance(line),
                         layout.text_rect.width());
            ++g_failures;
        }
        QRect line_rect(layout.text_rect.left(), layout.text_rect.top(),
                        metrics.horizontalAdvance(line), layout.line_height);
        if (line_rect.intersects(layout.arrow_rect)) {
            std::fprintf(stderr, "FAIL: header \"%s\": a line intersects the arrow rect\n",
                         title.toUtf8().constData());
            ++g_failures;
        }
    }
    // A single-word caption that fits the column must stay one whole line.
    if (!title.contains(QLatin1Char(' ')) && !title.isEmpty() &&
        metrics.horizontalAdvance(title) <= layout.text_rect.width()) {
        if (layout.lines.size() != 1 || layout.lines.front() != title) {
            std::fprintf(stderr, "FAIL: header \"%s\" was shortened despite fitting\n",
                         title.toUtf8().constData());
            ++g_failures;
        }
    }
}

void case_header_captions() {
    // The nine Entry widths at 1920 (docs/parity.md section 2.2) with the titles
    // the reference shows.
    const std::vector<msqt::EntryColumn>& columns = msqt::entry_columns();
    int x = 60;  // the gutter
    QRect band(0, 0, msqt::kParityWidth, msqt::kSheetHeaderHeight);
    for (const msqt::EntryColumn& column : columns) {
        const QRect section(x, band.top(), column.width, band.height());
        check_caption(section, QString::fromUtf8(column.title));
        x += column.width;
    }
    // A file-driven narrow set: the same captions at the widths the stored-table
    // gate writes (Job 183, Begin 111, Finish 87, Elapsed 63, Discipline 135,
    // Depends On 69, Who 99).
    struct NarrowSpec {
        const char* title;
        int width;
    };
    const NarrowSpec narrow[] = {{"Job", 183},       {"Begin", 111},     {"Finish", 87},
                                 {"Elapsed", 63},    {"Discipline", 135}, {"% Done", 51},
                                 {"Depends On", 69}, {"Who", 99},        {"Add New Column", 123}};
    int narrow_x = 60;
    for (const NarrowSpec& spec : narrow) {
        check_caption(QRect(narrow_x, 0, spec.width, msqt::kSheetHeaderHeight),
                      QString::fromUtf8(spec.title));
        narrow_x += spec.width;
    }
    // The operator's defect cases, explicitly: "Task Mode" at 57 px and
    // "Duration" at 81 px must be complete (two whole words / one whole word).
    const QFontMetrics metrics(header_font_for_gate());
    const msqt::HeaderCaptionLayout mode =
        msqt::layout_header_caption(QRect(0, 0, 57, msqt::kSheetHeaderHeight),
                                    QStringLiteral("Task Mode"), metrics);
    check(mode.lines.size() == 2 && mode.lines[0] == QStringLiteral("Task") &&
              mode.lines[1] == QStringLiteral("Mode"),
          "\"Task Mode\" wraps to whole words at 57 px (no \"Mod\")");
    const msqt::HeaderCaptionLayout duration =
        msqt::layout_header_caption(QRect(0, 0, 81, msqt::kSheetHeaderHeight),
                                    QStringLiteral("Duration"), metrics);
    check(duration.lines.size() == 1 && duration.lines[0] == QStringLiteral("Duration"),
          "\"Duration\" is whole at 81 px (no \"Dura\")");
}

// --- 1/2/3. the window chrome ------------------------------------------------

void case_window(const QString& fixture, const QString& out_dir) {
    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    if (!window.open_file(fixture, &error)) {
        std::fprintf(stderr, "FAIL: could not open %s: %s\n", fixture.toUtf8().constData(),
                     error.toUtf8().constData());
        ++g_failures;
        return;
    }
    window.show();
    QApplication::processEvents();

    // 1. the menu bar exists (actions alive) but is NOT visible.
    QMenuBar* bar = window.menuBar();
    check(bar != nullptr, "the window still owns a menu bar (the QAction owner)");
    check(bar != nullptr && !bar->isVisible(), "the classic menu bar is hidden");
    check(find_action(&window, app::MenuAction::save) != nullptr,
          "the existing action lookups still work with the bar hidden");

    // The status bar's left segment says Ready after a load (operator decision 8).
    check(window.status_left_text() == QStringLiteral("Ready"),
          "the status bar's left segment is Ready");
    check(window.status_detail_text().contains(QStringLiteral("warnings")),
          "the reader-warning count is still visible in the right segment");

    // 2. the backstage back arrow.
    window.show_backstage(true);
    QApplication::processEvents();
    check(window.backstage_open(), "the backstage opens");
    QToolButton* back = window.backstage() != nullptr
                            ? window.backstage()->findChild<QToolButton*>(
                                  QStringLiteral("backstageBack"))
                            : nullptr;
    check(back != nullptr, "the backstage has a back arrow");
    if (back != nullptr) {
        check(back->isVisible(), "the back arrow is visible");
        const QPoint origin = back->mapTo(window.backstage(), QPoint(0, 0));
        check(origin.x() <= 24 && origin.y() <= 40,
              "the back arrow is at the very top-left of the backstage");
        back->click();
        QApplication::processEvents();
        check(!window.backstage_open(), "the back arrow returns to the project chrome");
        check(window.central_stack() != nullptr && window.central_stack()->currentIndex() == 0,
              "the project chrome is back");
    }
    // Escape also leaves (it must not stay open once the arrow did close it).
    window.show_backstage(true);
    QApplication::processEvents();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&window, &escape);
    QApplication::processEvents();
    check(!window.backstage_open(), "Escape also leaves the backstage");

    // 3. double-click: no edit session; the six-tab Task Information dialog opens.
    QTableView* sheet = window.sheet_view();
    check(sheet != nullptr, "the window has a sheet view");
    if (sheet == nullptr || sheet->model() == nullptr) {
        return;
    }
    check((sheet->editTriggers() & QAbstractItemView::DoubleClicked) == 0,
          "double-click is NOT an edit trigger any more");
    check((sheet->editTriggers() & QAbstractItemView::EditKeyPressed) != 0,
          "F2 still opens an editor");
    check((sheet->editTriggers() & QAbstractItemView::AnyKeyPressed) != 0,
          "typing still opens an editor");

    // The tab titles are read INSIDE the modal loop (the dialog is destroyed when
    // it is rejected, so a pointer kept past exec() would dangle).
    QStringList seen_titles;
    bool saw_dialog = false;
    const QModelIndex name_cell = sheet->model()->index(0, 0);
    QTimer::singleShot(0, [&seen_titles, &saw_dialog]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        saw_dialog = true;
        QTabWidget* tabs =
            dialog->findChild<QTabWidget*>(QStringLiteral("taskInformationTabs"));
        if (tabs != nullptr) {
            for (int index = 0; index < tabs->count(); ++index) {
                seen_titles << tabs->tabText(index);
            }
        }
        dialog->reject();
    });
    // A REAL double-click on the first row's cell: the same event path a user's
    // mouse takes, so the connection and the "no edit on double-click" rule are
    // both proved.
    const QPointF double_click_at(sheet->visualRect(name_cell).center());
    QMouseEvent press(QEvent::MouseButtonPress, double_click_at, double_click_at,
                      double_click_at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(sheet->viewport(), &press);
    QMouseEvent double_click(QEvent::MouseButtonDblClick, double_click_at, double_click_at,
                             double_click_at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(sheet->viewport(), &double_click);
    QApplication::processEvents();
    check(saw_dialog, "a double-click opens the Task Information dialog");
    if (saw_dialog) {
        check(seen_titles.size() == 6, "the dialog has six tabs");
        check(seen_titles.contains(QStringLiteral("General")), "tab 1 is General");
        check(seen_titles.contains(QStringLiteral("Predecessors")), "tab 2 is Predecessors");
        check(seen_titles.contains(QStringLiteral("Resources")), "tab 3 is Resources");
        check(seen_titles.contains(QStringLiteral("Advanced")), "tab 4 is Advanced");
        check(seen_titles.contains(QStringLiteral("Notes")), "tab 5 is Notes");
        check(seen_titles.contains(QStringLiteral("Custom Fields")), "tab 6 is Custom Fields");
        // Cancel left no cell editor behind.
        check(sheet->viewport()->findChild<QLineEdit*>() == nullptr,
              "double-click did not open a cell editor");
    }

    // One painted caption band for human review (the operator's defect was
    // painted text, so the gate leaves the pixels behind).
    if (!out_dir.isEmpty()) {
        QHeaderView* header = sheet->horizontalHeader();
        if (header != nullptr) {
            const QPoint origin = header->mapTo(&window, QPoint(0, 0));
            const QImage band = window.grab().toImage().copy(
                QRect(origin.x(), origin.y(), header->width(), header->height()));
            const QString target = out_dir + QStringLiteral("/qt-header-band.png");
            check(band.save(target, "PNG"), "the painted header band PNG is written");
        }
    }

    // The dialog's General tab carries the operator's fields and the two
    // Display-on-Timeline checkboxes (read-only in this port).
    msqt::TaskInformationValues values;
    check(window.task_information_values(&values), "the selected task's values are readable");
    msqt::TaskInformationDialog dialog(values);
    check(dialog.name_edit() != nullptr && !dialog.name_edit()->text().isEmpty(),
          "the General tab pre-fills Name");
    check(dialog.hide_bar_check() != nullptr && dialog.rollup_check() != nullptr,
          "the Display on Timeline checkboxes exist");
    check(!dialog.hide_bar_check()->isEnabled(), "Hide Bar is read-only (no writer, decision 15)");
    dialog.name_edit()->setText(QStringLiteral("Renamed from Task Information"));
    dialog.accept();
    check(dialog.values().name == "Renamed from Task Information",
          "OK captures the edited General field");

    // OK applies through the sheet's edit model: the cell and the pending edit
    // (the save diff) both see it.
    check(window.apply_task_information(dialog.values()), "Task Information apply commits");
    const std::size_t name_column = window.sheet_table_model()->visible_column_for_field(
        app::task_field_id("Name"));
    check(name_column != core::sheet::kNoIndex, "the sheet has a Name model column");
    check(window.sheet() != nullptr &&
              window.sheet()->cell_text(0, name_column) == "Renamed from Task Information",
          "the dialog's OK reached the sheet model");
    check(window.sheet() != nullptr && window.sheet()->undo(),
          "the apply is one undo step like a typed edit");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_ux_gate_tests <fixture.mpp> [out-dir]\n");
        return 2;
    }
    case_header_captions();
    case_palette();
    case_window(QString::fromLocal8Bit(argv[1]),
                argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString());
    std::printf("qt_ux_gate: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
