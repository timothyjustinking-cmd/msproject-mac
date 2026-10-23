// app_qt/tests/parity_test.cpp -- the numeric acceptance test for the Qt shell
// against P1's measured baseline (docs/parity.md section 2).
//
// Runs the real MainWindow offscreen against the committed fixture and asserts:
//
//   row height 22, sheet header 42, the FROZEN row-number gutter 60 px,
//   Entry column widths 39/57/147/81/81/81/105/105/123 within 2 px each,
//   splitter at 881/1920 = 0.459 with a visible horizontal scrollbar,
//   24 px per day, a two-tier 21+21 timescale with a weekly label above
//   single-letter day cells,
//   the measured colours and Calibri-11 typography,
//   the ribbon (7 tabs incl. "Gantt Chart Format"), every visible button wide
//   enough for its whole label, the timeline strip and the status bar's zoom
//   slider (wired to the zoom actions),
//   and the 1920x1032 parity size: all nine Entry columns fit, so P1's
//   column-width comparison becomes comparable (wave 18 Q3), and the Indicators
//   cells are blank so no icon reads as a tenth grid line.
//
// Run it with QT_QPA_PLATFORM=offscreen (app_qt/CMakeLists.txt does).
#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QPixmap>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QString>
#include <QTableView>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "entry_columns.h"
#include "gantt_canvas.h"
#include "main_window.h"
#include "ribbon_bar.h"
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

void check_near(double value, double target, double tolerance, const char* what) {
    if (std::fabs(value - target) > tolerance) {
        std::fprintf(stderr, "FAIL: %s (got %.3f, want %.3f +/- %.3f)\n", what, value,
                     target, tolerance);
        ++g_failures;
    }
}

bool family_is_pinned(const QString& family) {
    for (int index = 0; index < msqt::kFontFamilyCount; ++index) {
        if (family.compare(QString::fromLatin1(msqt::kFontFamilies[index]),
                           Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool is_day_letter(const std::string& text) {
    return text.size() == 1 && text.find_first_of("SMTWFS") != std::string::npos;
}

// Every visible ribbon button must be at least as wide as its whole label (plus
// a pixel of slack) and must lie wholly inside the ribbon: Qt clips a
// QToolButton's text from both ends when the button is narrower ("Cu", "t F").
void check_ribbon_labels(msqt::RibbonBar* ribbon, int& visible_buttons) {
    if (ribbon == nullptr) {
        return;
    }
    const QList<QToolButton*> buttons = ribbon->findChildren<QToolButton*>();
    for (QToolButton* button : buttons) {
        if (button == nullptr || button->objectName() != QStringLiteral("ribbonButton") ||
            !button->isVisible()) {
            continue;
        }
        ++visible_buttons;
        const int label_width = button->fontMetrics().horizontalAdvance(button->text());
        check(button->width() >= label_width + 2, "a visible ribbon button fits its label");
        const QPoint top_left = button->mapTo(ribbon, QPoint(0, 0));
        check(top_left.x() >= 0 && top_left.x() + button->width() <= ribbon->width(),
              "a visible ribbon button is wholly inside the ribbon");
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_parity_tests <fixture.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);

    QString error;
    if (!window.open_file(fixture, &error)) {
        const QByteArray utf8 = error.toUtf8();
        std::fprintf(stderr, "FAIL: could not open %s: %s\n", argv[1], utf8.constData());
        return 1;
    }
    window.show();
    QApplication::processEvents();

    // --- typography --------------------------------------------------------
    check(QApplication::font().pointSize() == msqt::kBodyPointSize,
          "application font is the pinned 11 pt");
    check(!msqt::ui_font_family().isEmpty(), "a UI font family was resolved");
    // Carlito first, then the fixed list; a minimal container may have none of
    // them and keeps the platform family, which the handoff reports.
    check(family_is_pinned(msqt::ui_font_family()) ||
              msqt::ui_font_family() == QApplication::font().family(),
          "the UI font family is pinned (or the documented platform fallback)");

    // --- sheet geometry ----------------------------------------------------
    QTableView* sheet = window.sheet_view();
    check(sheet != nullptr, "the window owns a sheet view");
    if (sheet == nullptr) {
        std::printf("qt_parity_geometry: %d failure(s)\n", g_failures);
        return 1;
    }
    check(sheet->verticalHeader() != nullptr, "the sheet keeps its row-height source");
    check(sheet->horizontalHeader() != nullptr, "the sheet has a horizontal header");
    if (sheet->verticalHeader() != nullptr) {
        check(sheet->verticalHeader()->defaultSectionSize() == msqt::kRowHeight,
              "row height is 22 px");
        // The row-number gutter is the FROZEN row header (wave 18 Q3): it must
        // be visible, 60 px, and must not scroll horizontally with the columns.
        check(sheet->verticalHeader()->isVisible(),
              "the row-number gutter is a visible, frozen row header");
        check(sheet->verticalHeader()->width() == msqt::kGutterWidth,
              "row-number gutter is 60 px");
    }
    if (sheet->horizontalHeader() != nullptr) {
        check(sheet->horizontalHeader()->height() == msqt::kSheetHeaderHeight,
              "sheet header band is 42 px");
    }

    // --- Entry columns, each within 2 px of the reference -------------------
    const std::vector<msqt::EntryColumn>& columns = msqt::entry_columns();
    check(columns.size() == 9, "the Entry table shows its 9 measured columns");
    check(sheet->model() != nullptr && sheet->model()->columnCount() == 9,
          "the table model exposes the 9 Entry columns (the gutter is the row header)");

    // The widths come from the file's `214/CTable` width bytes, through MS
    // Project's character rule (6 px/char + 3): 24 chars -> 147 px.
    check(window.table_widths_read(), "the fixture's 214/CTable widths were read");
    check(window.table_column_chars(0x0b40000eu) == 24, "Task Name stores 24 characters");
    check(window.table_column_chars(0x0b400507u) == 13, "Duration stores 13 characters");
    check(window.table_column_chars(0x0b40002fu) == 17, "Predecessors stores 17 characters");
    check(msqt::entry_width_px(24) == 147, "24 characters -> 147 px (6 px + 3 padding)");

    for (std::size_t index = 0; index < columns.size(); ++index) {
        const int width = sheet->columnWidth(static_cast<int>(index));
        if (std::abs(width - columns[index].width) > 2) {
            std::fprintf(stderr, "FAIL: column %s width %d, want %d +/- 2\n",
                         columns[index].title[0] ? columns[index].title : "(indicators)", width,
                         columns[index].width);
            ++g_failures;
        }
    }
    check(msqt::entry_content_width() == 819, "the Entry content width is 819 px");

    // --- splitter ----------------------------------------------------------
    check(window.splitter() != nullptr, "the window has a sheet/Gantt splitter");
    check_near(window.split_fraction(), msqt::kSplitFraction, 0.02,
               "splitter is at 881/1920 = 0.459 of the client width");
    check(sheet->horizontalScrollBar() != nullptr &&
              sheet->horizontalScrollBar()->isVisible(),
          "the visible horizontal scrollbar under the sheet");

    // --- Gantt timescale ---------------------------------------------------
    msqt::GanttCanvas* gantt = window.gantt_view();
    check(gantt != nullptr, "the window owns a Gantt canvas");
    if (gantt != nullptr) {
        const msqt::GanttHeaderInfo info = gantt->header_info(gantt->width());
        check(info.header_height == msqt::kGanttHeaderHeight, "Gantt header band is 42 px");
        check(info.tier_height == msqt::kGanttTierHeight, "each timescale tier is 21 px");
        check(info.header_height == info.tier_height * 2, "the timescale has two tiers");
        check_near(info.day_width, msqt::kPixelsPerDay, 0.51, "day width is 24 px");
        check(info.major_cells >= 1, "at least one weekly tier label is visible");
        check(info.minor_cells >= 7, "at least one full week of day cells is visible");
        check(info.first_major_label.find('\'') != std::string::npos,
              "the weekly label carries the year ('26)");
        check(is_day_letter(info.first_minor_label),
              "the day cells are single weekday letters (S M T W T F S)");
    }

    // --- colours (the measured reference values) ---------------------------
    check(msqt::kHeaderBackground == QColor(0xff, 0xff, 0xff), "header fill is white");
    check(msqt::kGridLine == QColor(0xd4, 0xd4, 0xd4), "grid line is (212,212,212)");
    check(msqt::kWeekend == QColor(0xf7, 0xf7, 0xf7), "weekend shading is (247,247,247)");
    check(msqt::kSelection == QColor(0xdc, 0xe9, 0xf5), "selection is #dce9f5");
    check(msqt::kBarTask == QColor(0x4a, 0x7e, 0xbb), "task bar is #4a7ebb (unmeasured)");

    // --- chrome ------------------------------------------------------------
    msqt::RibbonBar* ribbon = window.ribbon();
    check(ribbon != nullptr, "the window has a ribbon bar");
    int visible_buttons_900 = 0;
    if (ribbon != nullptr) {
        check(ribbon->tab_count() == 7, "the ribbon has 7 tabs");
        check(ribbon->tab_titles().contains(QStringLiteral("Task")),
              "the ribbon's first tab is Task");
        check(ribbon->tab_titles().contains(QStringLiteral("Gantt Chart Format")),
              "the ribbon has the contextual Gantt Chart Format tab");
        check(ribbon->button_count() > 0, "the active ribbon tab shows its buttons");
        check(!ribbon->group_titles().isEmpty(), "ribbon groups have captions");
        check(ribbon->visible_group_count() > 0,
              "at 900 px the ribbon still shows at least one whole group");
        check_ribbon_labels(ribbon, visible_buttons_900);
        check(visible_buttons_900 > 0, "at 900 px at least one ribbon button is visible");
    }
    check(window.timeline() != nullptr, "the window has a timeline strip");
    if (window.timeline() != nullptr) {
        check(window.timeline()->height() == msqt::kTimelineHeight,
              "the timeline strip is 44 px tall");
        check(window.timeline()->centre_text().contains(QStringLiteral("timeline")),
              "the timeline prompts for tasks with dates");
    }

    // --- status bar zoom slider is wired to the zoom actions ---------------
    QSlider* zoom = window.zoom_slider();
    check(zoom != nullptr, "the status bar has a zoom slider");
    if (zoom != nullptr) {
        check(zoom->minimum() == 0 && zoom->maximum() == 2, "the zoom slider spans day..month");
        check(zoom->value() == static_cast<int>(app::ZoomLevel::week),
              "the default zoom is the weekly view");
        if (gantt != nullptr) {
            zoom->setValue(static_cast<int>(app::ZoomLevel::day));
            QApplication::processEvents();
            const double day_width = gantt->header_info(gantt->width()).day_width;
            check_near(day_width, 20.0, 0.51, "the day zoom is 20 px/day");
            zoom->setValue(static_cast<int>(app::ZoomLevel::week));
            QApplication::processEvents();
            check_near(gantt->header_info(gantt->width()).day_width, msqt::kPixelsPerDay, 0.51,
                       "the slider returns the Gantt to the measured 24 px/day");
        }
    }

    // --- the gutter is frozen, not a scrolling column ----------------------
    if (sheet->horizontalScrollBar() != nullptr && sheet->verticalHeader() != nullptr) {
        sheet->horizontalScrollBar()->setValue(120);
        QApplication::processEvents();
        check(sheet->verticalHeader()->isVisible() &&
                  sheet->verticalHeader()->width() == msqt::kGutterWidth,
              "the row-number gutter stays put when the sheet scrolls sideways");
        check(sheet->columnViewportPosition(0) < 0,
              "the sheet's columns scroll under the frozen gutter");
        sheet->horizontalScrollBar()->setValue(0);
        QApplication::processEvents();
    }

    // --- the reference's own size: the whole Entry table must fit ----------
    // At 900 px only four columns fit, which is why P1's harness reports the
    // column widths N/A.  At the reference's 1920x1032 all nine must be on
    // screen, so the comparison is 9 columns against 9.
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    QApplication::processEvents();
    QApplication::processEvents();
    check(window.splitter() != nullptr, "the splitter survives the resize");
    if (window.splitter() != nullptr && sheet->verticalHeader() != nullptr) {
        const int sheet_pane = window.splitter()->sizes().value(0);
        check(sheet_pane >= msqt::kGutterWidth + msqt::entry_content_width() - 1,
              "at 1920x1032 the sheet pane holds the whole Entry table");
        check_near(window.split_fraction(), msqt::kSplitFraction, 0.02,
                   "the splitter still holds 0.459 at the reference size");
        const int content_right = sheet->columnViewportPosition(columns.size() - 1) +
                                  sheet->columnWidth(static_cast<int>(columns.size()) - 1);
        std::fprintf(stderr, "note: 1920 pane %d, viewport %d, table right %d\n", sheet_pane,
                     sheet->viewport()->width(), content_right);
        check(content_right <= sheet->viewport()->width() + 1,
              "all nine Entry columns are visible at 1920x1032");
        check(sheet->verticalHeader()->width() == msqt::kGutterWidth,
              "the frozen gutter is still 60 px at 1920x1032");
    }
    int visible_buttons_1920 = 0;
    check_ribbon_labels(ribbon, visible_buttons_1920);
    if (ribbon != nullptr) {
        check(ribbon->visible_group_count() >= 1,
              "at 1920 the ribbon shows whole groups, never a cut label");
        std::fprintf(stderr, "note: visible ribbon groups %d at 1920, buttons %d\n",
                     ribbon->visible_group_count(), visible_buttons_1920);
    }

    // --- the Indicators cells stay blank -----------------------------------
    // P1's column detector calls a body-band column a grid line when it differs
    // from its row's background for >= 80 % of the band.  A tall icon in the
    // 39 px Indicators column does exactly that: the 16 px (i) badge of wave 17
    // made the real 1920 render measure 10 sheet columns instead of 9 (CI run
    // f4a4e4a).  The reference's Indicators cells are blank, so blank is both
    // faithful and the only safe content.  This is the gate that keeps it so.
    if (sheet->viewport() != nullptr && sheet->model() != nullptr &&
        sheet->model()->rowCount() > 0) {
        const QImage image = window.grab().toImage();
        // CTest pins QT_SCALE_FACTOR=1, so the grab is 1:1; say so rather than
        // sampling the wrong pixels if that ever changes.
        check(image.size() == QSize(window.width(), window.height()),
              "the 1920 grab is 1:1 with the window");
        const QPoint viewport_origin = sheet->viewport()->mapTo(&window, QPoint(0, 0));
        const int column_width = sheet->columnWidth(0);
        bool blank = true;
        for (int row = 0; row < sheet->model()->rowCount() && blank; ++row) {
            // Inside the row: skip the top rule and the bottom grid line.
            const int top = viewport_origin.y() + row * msqt::kRowHeight + 2;
            const int bottom = viewport_origin.y() + (row + 1) * msqt::kRowHeight - 2;
            for (int x = viewport_origin.x() + 2;
                 x < viewport_origin.x() + column_width - 2 && blank; ++x) {
                for (int y = top; y < bottom; ++y) {
                    const QColor colour = image.pixelColor(x, y);
                    if (std::min({colour.red(), colour.green(), colour.blue()}) < 200) {
                        blank = false;
                        break;
                    }
                }
            }
        }
        check(blank, "the Indicators cells are blank (no icon reads as a grid line)");
    }

    std::printf("qt_parity_geometry: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
