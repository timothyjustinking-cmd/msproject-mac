// app_qt/tests/empty_state_test.cpp -- wave 20's structural gate for the EMPTY
// project, at the reference's 1920x1032 and at the 900x560 window.
//
// The operator's screenshot of the packaged build showed the defects this test
// would have failed:
//   (a) the Gantt timescale began at the engine epoch (2 Jan '84), not today;
//   (b) the sheet had NO column headers on an empty project;
//   (c) the timeline strip was centred text, with no Start/Finish block;
//   (d) the status bar was flat text with a bare slider (no view icons/percent);
//   (e) the ribbon's groups/buttons had no geometry contract and no icons.
// Plus the chrome the screenshot found missing: quick-access toolbar, rotated
// "GANTT CHART" label, visible scrollbars and the manual-scheduling callout.
//
// These are presence/position/size assertions.  No reference image is stored or
// compared (the reference is third-party UI; see docs/ui-qt-port.md section 7).
#include <QAbstractItemModel>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFontMetrics>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPalette>
#include <QPoint>
#include <QPixmap>
#include <QScrollBar>
#include <QSize>
#include <QSlider>
#include <QStackedWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "backstage.h"
#include "entry_columns.h"
#include "gantt_canvas.h"
#include "layout_model.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "shell_style.h"
#include "theme.h"
#include "timeline_strip.h"
#include "view_label.h"

#include "gate_util.h"

namespace {

// Column 0 is the Indicators field: the reference capture shows the blue (i)
// glyph on a selected cell and NO caption text, so its expected caption is
// empty.  The other eight carry the product's words.  (This is the reference's
// own header band, cropped and read locally; the image is never committed.)
const char* const kExpectedTitles[9] = {"",              "Task Mode", "Task Name",
                                        "Duration",      "Start",     "Finish",
                                        "Predecessors",  "Resource Names",
                                        "Add New Column"};

void check_header_band(msqt::MainWindow& window) {
    QTableView* sheet = window.sheet_view();
    gate::check(sheet != nullptr, "the empty project owns a sheet view");
    if (sheet == nullptr) {
        return;
    }
    gate::check(sheet->horizontalHeader() != nullptr, "the empty project has a header band");
    if (sheet->horizontalHeader() != nullptr) {
        gate::check(sheet->horizontalHeader()->isVisible(),
                    "the empty project's header band is visible (not a blank strip)");
        gate::check(sheet->horizontalHeader()->height() == msqt::kSheetHeaderHeight,
                    "the empty header band is 42 px");
    }
    QAbstractItemModel* model = sheet->model();
    gate::check(model != nullptr, "the empty project has a table model");
    if (model == nullptr) {
        return;
    }
    gate::check(model->rowCount() == 1,
                "the empty project shows the product's first (empty) row");
    gate::check(model->columnCount() == 9, "the empty project shows the 9 Entry columns");
    const std::vector<msqt::EntryColumn>& columns = msqt::entry_columns();
    int x = 0;
    for (int index = 0; index < model->columnCount() && index < 9; ++index) {
        const QString title =
            model->headerData(index, Qt::Horizontal, Qt::DisplayRole).toString();
        gate::check(title == QString::fromUtf8(kExpectedTitles[index]),
                    "the empty header carries the expected Entry caption");
        if (index < static_cast<int>(columns.size())) {
            gate::check(sheet->columnWidth(index) == columns[static_cast<std::size_t>(index)].width,
                        "the empty column has the stock Entry width");
            gate::check(sheet->columnViewportPosition(index) == x,
                        "the empty column starts at the cumulative measured x");
            x += sheet->columnWidth(index);
        }
    }
    gate::check(sheet->horizontalScrollBar() != nullptr &&
                    sheet->horizontalScrollBar()->isVisible(),
                "the sheet's horizontal scrollbar is visible");
}

void check_timescale_origin(msqt::MainWindow& window) {
    msqt::GanttCanvas* gantt = window.gantt_view();
    gate::check(gantt != nullptr, "the empty project owns a Gantt canvas");
    if (gantt == nullptr) {
        return;
    }
    const core::sched::date32 origin = gantt->layout().timescale.origin;
    const core::sched::date32 today = app::today_date32();
    gate::check(origin != core::sched::from_ymd(1984, 1, 2),
                "the empty timescale does NOT start at the 1984 engine epoch");
    gate::check(core::sched::weekday(origin) == 0,
                "the empty timescale starts on the Sunday of the current week");
    gate::check(!core::sched::is_na(origin) && origin <= today &&
                    core::sched::add_days(origin, 7) > today,
                "today lies inside the first empty-project week");
    gate::check(origin >= core::sched::from_ymd(2024, 1, 1),
                "the empty axis is in the present, not 1984");
}

void check_timeline(msqt::MainWindow& window) {
    msqt::TimelineStrip* timeline = window.timeline();
    gate::check(timeline != nullptr, "the window has a timeline strip");
    if (timeline == nullptr) {
        return;
    }
    gate::check(timeline->height() == msqt::kTimelineHeight,
                "the timeline strip has the pinned height");
    gate::check(timeline->has_timeline_label(),
                "the timeline has its vertical TIMELINE label");
    gate::check(timeline->timeline_label_rect().x() == 0,
                "the TIMELINE label sits on the strip's left edge");
    gate::check(timeline->has_date_block(),
                "the timeline has a Start/Finish block (not centred text only)");
    const QRect block = timeline->start_block_rect();
    const QRect bar = timeline->timeline_bar_rect();
    gate::check(block.x() == msqt::kTimelineLabelWidth + 8,
                "the Start/Finish block follows the TIMELINE label");
    gate::check(block.width() >= 160, "the Start/Finish block is wide enough for two dates");
    gate::check(block.height() >= 24, "the Start/Finish block has a two-line height");
    gate::check(bar.left() > block.right(), "the timeline bar follows the date block");
    gate::check(bar.width() > 100, "the timeline bar is a real bar, not a stub");
    gate::check(!timeline->start_text().isEmpty(), "the empty project shows today as Start");
    gate::check(timeline->start_text() == timeline->finish_text(),
                "a new project's Start and Finish are the same day");
}

void check_status_bar(msqt::MainWindow& window) {
    const std::vector<QToolButton*>& buttons = window.view_buttons();
    gate::check(buttons.size() == 4, "the status bar has four view buttons");
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        QToolButton* button = buttons[index];
        gate::check(button != nullptr, "a view button exists");
        if (button == nullptr) {
            continue;
        }
        gate::check(button->isVisible(), "a view button is visible");
        gate::check(!button->text().isEmpty(), "a view button has its label");
        gate::check(!button->icon().isNull(), "a view button has an icon");
        gate::check(button->toolButtonStyle() == Qt::ToolButtonTextBesideIcon,
                    "a view button shows icon + label");
        gate::check(button->height() >= 18, "a view button fits the 24 px status bar");
    }
    QSlider* zoom = window.zoom_slider();
    gate::check(zoom != nullptr && zoom->isVisible(), "the zoom slider is visible");
    gate::check(zoom != nullptr && zoom->minimum() == 0 && zoom->maximum() == 2,
                "the zoom slider spans day..month");
    gate::check(zoom != nullptr && zoom->value() == static_cast<int>(app::ZoomLevel::week),
                "the empty window opens at the weekly zoom");
    // The zoom group is minus / slider / plus, with no percentage readout.
    gate::check(window.status_mode_label() != nullptr &&
                    window.status_mode_label()->text().contains(QStringLiteral("New Tasks")),
                "the status bar centre shows the scheduling mode");
}

void check_quick_access(msqt::MainWindow& window) {
    QToolBar* quick = window.quick_access();
    gate::check(quick != nullptr, "the window has a quick-access toolbar");
    if (quick == nullptr) {
        return;
    }
    gate::check(quick->isVisible(), "the quick-access toolbar is visible");
    gate::check(quick->actions().size() == 3, "the quick-access toolbar carries Save/Undo/Redo");
    for (QAction* action : quick->actions()) {
        gate::check(action != nullptr && !action->icon().isNull(),
                    "every quick-access action has an icon");
    }
}

void check_chart_chrome(msqt::MainWindow& window) {
    gate::check(window.chart_panel() != nullptr && window.chart_panel()->isVisible(),
                "the chart pane is visible in the task view");
    msqt::ViewLabel* label = window.view_label();
    gate::check(label != nullptr, "the chart pane has a rotated view caption");
    if (label != nullptr) {
        gate::check(label->isVisible(), "the rotated view caption is visible");
        gate::check(label->width() == msqt::kViewLabelWidth,
                    "the rotated caption is the pinned 22 px strip");
        gate::check(label->height() > 100, "the rotated caption spans the chart height");
        gate::check(!label->text().isEmpty(), "the rotated caption names the view");
    }
    QLabel* callout = window.info_callout();
    gate::check(callout != nullptr && callout->isVisible(),
                "the new-project manual-scheduling callout is visible");
    if (callout != nullptr) {
        gate::check(callout->height() == msqt::kInfoCalloutHeight,
                    "the callout is the pinned 24 px band");
        gate::check(callout->text().contains(QStringLiteral("Manually Scheduled")),
                    "the callout carries the manual-scheduling sentence");
    }
}

void check_ribbon(msqt::MainWindow& window, int width, bool expect_all_groups) {
    msqt::RibbonBar* ribbon = window.ribbon();
    gate::check(ribbon != nullptr, "the window has a ribbon");
    if (ribbon == nullptr) {
        return;
    }
    gate::check(ribbon->height() == msqt::kRibbonHeight + 1, "the ribbon is the pinned height");
    gate::check(ribbon->tab_count() == 7, "the ribbon keeps its seven command tabs");
    gate::check(ribbon->file_tab() != nullptr, "the ribbon has a File tab");
    gate::check(ribbon->file_tab() != nullptr &&
                    ribbon->file_tab()->text() == QStringLiteral("File"),
                "the File tab is labelled");
    gate::check(ribbon->tell_me_text() == QStringLiteral("Tell me what you want to do"),
                "the ribbon has the Tell me box");

    // Group rectangles: a whole group is either on screen or hidden; a visible
    // group is wholly inside the ribbon and its caption is not clipped.
    const QList<QLabel*> captions =
        ribbon->findChildren<QLabel*>(QStringLiteral("ribbonGroupLabel"));
    gate::check(!captions.isEmpty(), "the active ribbon tab has groups");
    int visible_groups = 0;
    bool hidden_seen = false;
    bool prefix_ok = true;
    for (QLabel* caption : captions) {
        QWidget* box = caption != nullptr ? caption->parentWidget() : nullptr;
        if (box == nullptr) {
            continue;
        }
        const bool visible = box->isVisible();
        if (visible) {
            ++visible_groups;
            if (hidden_seen) {
                prefix_ok = false;
            }
            const QRect mapped(box->mapTo(ribbon, QPoint(0, 0)), box->size());
            gate::check(gate::inside(ribbon->rect(), mapped),
                        "a visible ribbon group is wholly inside the ribbon");
            gate::check(box->width() >= box->sizeHint().width() - 1,
                        "a visible ribbon group keeps its measured width");
            gate::check(caption->width() <= box->width(),
                        "a group caption fits its group box");
            gate::check(caption->fontMetrics().horizontalAdvance(caption->text()) <=
                            caption->width() + 1,
                        "a group caption is not clipped");
            gate::check(!caption->text().isEmpty(), "a visible group has a caption");
        } else {
            hidden_seen = true;
        }
    }
    gate::check(prefix_ok, "visible ribbon groups are a prefix (no group after a hidden one)");
    gate::check(visible_groups >= 1, "at least one whole group is visible");
    if (expect_all_groups) {
        gate::check(visible_groups == ribbon->group_titles().size(),
                    "at the reference width every group of the active tab is visible");
    }

    int visible_buttons = 0;
    for (QToolButton* button : ribbon->findChildren<QToolButton*>(
             QStringLiteral("ribbonButton"))) {
        if (button == nullptr || !button->isVisible()) {
            continue;
        }
        ++visible_buttons;
        gate::check(!button->icon().isNull(), "a visible ribbon button has an icon");
        gate::check(button->height() == msqt::kRibbonButtonHeight,
                    "a visible ribbon button has the pinned height");
        const int label_width = button->fontMetrics().horizontalAdvance(button->text());
        gate::check(button->width() >= label_width + 2,
                    "a visible ribbon button fits its label (no clipping)");
        const QRect mapped(button->mapTo(ribbon, QPoint(0, 0)), button->size());
        gate::check(gate::inside(ribbon->rect(), mapped),
                    "a visible ribbon button is wholly inside the ribbon");
        gate::check(button->toolButtonStyle() == Qt::ToolButtonTextUnderIcon,
                    "a ribbon button shows icon + label");
        gate::check(button->iconSize() == QSize(msqt::kRibbonIconPx, msqt::kRibbonIconPx),
                    "a ribbon button uses the measured 32 px icon");
    }
    gate::check(visible_buttons > 0, "the active tab shows its buttons");
    if (expect_all_groups) {
        gate::check(visible_buttons == ribbon->button_count(),
                    "at the reference width every button of the active tab is visible");
    }
    // The group row never wraps: a narrow window shows ONE ">>" chevron at the
    // right edge, and the tab strip clips behind its own chevron.
    gate::check(ribbon->overflow_button() != nullptr,
                "the ribbon has a group-overflow chevron");
    gate::check(ribbon->tab_overflow_button() != nullptr,
                "the ribbon has a tab-overflow chevron");
    if (expect_all_groups) {
        gate::check(!ribbon->overflow_button()->isVisible(),
                    "at 1920 the group-overflow chevron is hidden");
        gate::check(!ribbon->tab_overflow_button()->isVisible(),
                    "at 1920 the tab-overflow chevron is hidden");
    } else {
        gate::check(ribbon->overflow_button()->isVisible(),
                    "at 900 the group-overflow chevron is visible (no second row)");
        gate::check(ribbon->tab_overflow_button()->isVisible(),
                    "at 900 the tab-overflow chevron is visible");
        gate::check(ribbon->height() == msqt::kRibbonHeight + 1,
                    "the overflow chevron does not change the ribbon height");
    }
    std::fprintf(stderr, "note: ribbon at %d px: groups %d/%d, buttons %d/%d\n", width,
                 visible_groups, static_cast<int>(ribbon->group_titles().size()),
                 visible_buttons, ribbon->button_count());
}

void check_backstage(msqt::MainWindow& window) {
    msqt::Backstage* backstage = window.backstage();
    gate::check(backstage != nullptr, "the window has a File backstage");
    if (backstage == nullptr) {
        return;
    }
    const std::vector<QToolButton*>& nav = backstage->nav_buttons();
    gate::check(nav.size() == 11, "the backstage rail has eleven entries");
    const char* const labels[11] = {"Home",    "New",     "Open",  "Info",
                                    "Save",    "Save As", "Print", "Share",
                                    "Export",  "Close",   "Options"};
    for (std::size_t index = 0; index < nav.size() && index < 11; ++index) {
        gate::check(nav[index] != nullptr &&
                        nav[index]->text() == QString::fromUtf8(labels[index]),
                    "the backstage rail entry has the expected label");
    }
    gate::check(backstage->pages() != nullptr && backstage->pages()->count() == 11,
                "the backstage has eleven pages");
    // The rail's geometry only exists once the backstage is on screen; measure
    // it after the switch, not from an unlaid-out hidden widget.
    window.show_backstage(true);
    QApplication::processEvents();
    if (nav.size() == 11) {
        const int close_y = nav[9]->mapTo(backstage, QPoint(0, 0)).y();
        const int options_y = nav[10]->mapTo(backstage, QPoint(0, 0)).y();
        gate::check(options_y > close_y + 20,
                    "Options is pinned at the bottom of the rail");
    }
    gate::check(window.backstage_open() && backstage->isVisible(),
                "the File tab opens the backstage");
    gate::check(backstage->pages()->currentIndex() ==
                    static_cast<int>(msqt::Backstage::Page::home),
                "the backstage opens on Home");
    backstage->show_page(msqt::Backstage::Page::options);
    QApplication::processEvents();
    gate::check(backstage->pages()->currentIndex() ==
                    static_cast<int>(msqt::Backstage::Page::options),
                "the Options nav opens the Options page");
    gate::check(backstage->theme_combo() != nullptr && backstage->theme_combo()->isVisible(),
                "Options > General carries the Office theme choice");
    window.show_backstage(false);
    QApplication::processEvents();
    gate::check(!window.backstage_open(), "the backstage closes back to the chrome");
}

void check_themes(msqt::MainWindow& window) {
    window.set_theme(msqt::Theme::dark, false);
    gate::check(window.theme() == msqt::Theme::dark, "the dark theme is selected");
    gate::check(QApplication::palette().color(QPalette::Window) ==
                    msqt::theme_window_colour(msqt::Theme::dark),
                "the dark theme swaps the window palette");
    const QPixmap dark = window.grab();
    gate::check(dark.width() == window.width() && dark.height() == window.height(),
                "the dark theme renders at the window size");
    window.set_theme(msqt::Theme::light, false);
    gate::check(window.theme() == msqt::Theme::light, "the light theme is selected");
    gate::check(QApplication::palette().color(QPalette::Window) ==
                    msqt::theme_window_colour(msqt::Theme::light),
                "the light theme restores the parity palette");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    const QString out_dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
    }

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    gate::check(window.width() == msqt::kParityWidth &&
                    window.height() == msqt::kParityHeight,
                "the empty window is 1920x1032");
    check_timescale_origin(window);
    check_header_band(window);
    check_timeline(window);
    check_status_bar(window);
    check_quick_access(window);
    check_chart_chrome(window);
    check_ribbon(window, msqt::kParityWidth, true);

    const QPixmap parity = window.grab();
    gate::check(parity.width() == msqt::kParityWidth &&
                    parity.height() == msqt::kParityHeight,
                "the empty-project parity grab is 1920x1032");
    if (!out_dir.isEmpty()) {
        gate::check(parity.save(out_dir + QStringLiteral("/qt-empty-1920.png"), "PNG"),
                    "the 1920x1032 empty render is written");
    }

    // --- the same structure at the pinned 900x560 window --------------------
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QApplication::processEvents();
    QApplication::processEvents();
    gate::check(window.width() == msqt::kWindowWidth &&
                    window.height() == msqt::kWindowHeight,
                "the empty window is 900x560");
    check_timescale_origin(window);
    check_header_band(window);
    check_timeline(window);
    check_status_bar(window);
    check_quick_access(window);
    check_ribbon(window, msqt::kWindowWidth, false);
    const QPixmap small = window.grab();
    gate::check(small.width() == msqt::kWindowWidth && small.height() == msqt::kWindowHeight,
                "the empty-project 900x560 grab is the window size");
    if (!out_dir.isEmpty()) {
        gate::check(small.save(out_dir + QStringLiteral("/qt-empty-900.png"), "PNG"),
                    "the 900x560 empty render is written");
    }

    check_backstage(window);

    // Both themes at both measured sizes (decision 12): the palette swap is
    // asserted and the dark renders are written for human review.
    check_themes(window);
    if (!out_dir.isEmpty()) {
        window.set_theme(msqt::Theme::dark, false);
        const QPixmap dark_small = window.grab();
        gate::check(dark_small.save(out_dir + QStringLiteral("/qt-empty-dark-900.png"), "PNG"),
                    "the 900x560 dark render is written");
    }
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    QApplication::processEvents();
    QApplication::processEvents();
    window.set_theme(msqt::Theme::dark, false);
    const QPixmap dark_parity = window.grab();
    gate::check(dark_parity.width() == msqt::kParityWidth &&
                    dark_parity.height() == msqt::kParityHeight,
                "the 1920x1032 dark render is the window size");
    if (!out_dir.isEmpty()) {
        gate::check(
            dark_parity.save(out_dir + QStringLiteral("/qt-empty-dark-1920.png"), "PNG"),
            "the 1920x1032 dark render is written");
    }
    window.set_theme(msqt::Theme::light, false);

    std::printf("qt_empty_state: ");
    return gate::finish();
}
