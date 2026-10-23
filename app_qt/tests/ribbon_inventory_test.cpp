// app_qt/tests/ribbon_inventory_test.cpp -- wave 22 (operator decision 1): the
// seven ribbon tabs carry MS Project 2021's measured group/button inventory.
//
// The source of truth is the local, git-ignored reference set: the clean ribbon
// strips under corpus/reference/2026-10-23-msproject/
// (project-ribbon-{resource,report,project,view,help,gantt-format}.png, the Task
// tab from project-gantt-loaded-clean.png).  The measured inventory is pinned
// HERE, tab -> group -> buttons in ribbon order, with the modelled command of
// each button.  See docs/handoff/r6.md for the per-tab capture.
//
// What this gate asserts, for EVERY one of the seven tabs:
//   * tab_titles() is exactly the seven measured captions, in order;
//   * group_titles() is exactly the pinned group list, in order;
//   * each group's button labels and their MenuAction are exactly the pin;
//   * every button obeys the decision-9 rule: enabled iff shell_implements(action),
//     and the disabled tooltip is the exact "not implemented in this port";
//   * at 1920x1032 the visible groups are a PREFIX of the inventory (no group
//     after a hidden one), every visible caption/button is wholly inside the
//     ribbon and wide enough for its whole text, and the ">>" chevron is visible
//     exactly when the inventory overflows;
//   * at 900x560 the same prefix/fit/chevron contract holds.
//
// "All groups visible at 1920" is deliberately NOT the criterion: the inventory
// is.  Each tab's ">>" state is checked against its OWN visible/total groups, so
// a tab that overflows must show the chevron and a tab that fits must hide it;
// the 900 px pass proves the overflow path.  The Task tab must still fit at
// 1920 because the 9/9 UI-parity fixture and qt_empty_state measure it there.
//
// The gate also keeps the seven-tab count and the contextual rule: the
// "Gantt Chart Format" tab is in the model at all times but is only shown while
// the Gantt (task_sheet) view is active; switching to a grid view hides it and
// switching back shows it (through the real menu QAction path).
#include <QAction>
#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "command_status.h"
#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "shell_style.h"

namespace {

using app::MenuAction;

struct ButtonPin {
    const char* label;
    MenuAction action;
};

struct GroupPin {
    const char* title;
    std::vector<ButtonPin> buttons;
};

struct TabPin {
    const char* title;
    std::vector<GroupPin> groups;
};

// ---------------------------------------------------------------------------
// THE PIN.  Measured from the captures, never from the implementation:
//   Task               project-gantt-loaded-clean.png (ribbon band)
//   Resource           project-ribbon-resource.png (full 1910x986 window)
//   Report             project-ribbon-report.png
//   Project            project-ribbon-project.png
//   View               project-ribbon-view.png
//   Help               project-ribbon-help.png
//   Gantt Chart Format project-ribbon-gantt-format.png
// Exact MenuAction matches keep the command; everything else is none.
// ---------------------------------------------------------------------------
const std::vector<TabPin>& pinned_tabs() {
    static const std::vector<TabPin> kPins = {
        {"Task",
         {
             {"View", {{"Gantt Chart", MenuAction::none}}},
             {"Clipboard",
              {{"Paste", MenuAction::paste},
               {"Cut", MenuAction::cut},
               {"Copy", MenuAction::copy},
               {"Format Painter", MenuAction::none}}},
             // Font family and size are value displays in the capture (Arial /
             // 8); the port carries them as disabled text buttons.
             {"Font",
              {{"Arial", MenuAction::none},
               {"8", MenuAction::none},
               {"B", MenuAction::none},
               {"I", MenuAction::none},
               {"U", MenuAction::none}}},
             {"Schedule",
              {{"Mark on Track", MenuAction::none},
               {"Respect Links", MenuAction::none},
               {"Inactivate", MenuAction::none}}},
             {"Tasks",
              {{"Manually Schedule", MenuAction::none},
               {"Auto Schedule", MenuAction::none},
               {"Inspect", MenuAction::none},
               {"Move", MenuAction::none},
               {"Mode", MenuAction::none}}},
             {"Insert",
              {{"Task", MenuAction::insert_task},
               {"Summary", MenuAction::none},
               {"Milestone", MenuAction::none},
               {"Deliverable", MenuAction::none}}},
             {"Properties",
              {{"Information", MenuAction::task_fields},
               {"Notes", MenuAction::none},
               {"Details", MenuAction::none},
               {"Add to Timeline", MenuAction::none}}},
             {"Editing",
              {{"Scroll to Task", MenuAction::none},
               {"Find", MenuAction::show_filter},
               {"Clear", MenuAction::clear_filter},
               {"Fill", MenuAction::none}}},
         }},
        {"Resource",
         {
             {"View", {{"Team Planner", MenuAction::none}}},
             {"Assignments",
              {{"Assign Resources", MenuAction::none},
               {"Resource Pool", MenuAction::none}}},
             {"Insert", {{"Add Resources", MenuAction::insert_resource}}},
             {"Properties",
              {{"Information", MenuAction::none},
               {"Notes", MenuAction::none},
               {"Details", MenuAction::none}}},
             {"Level",
              {{"Level Selection", MenuAction::none},
               {"Level Resource", MenuAction::none},
               {"Level All", MenuAction::none},
               {"Leveling Options", MenuAction::none},
               {"Clear Leveling", MenuAction::none},
               {"Next Overallocation", MenuAction::none}}},
         }},
        {"Report",
         {
             {"Project", {{"Compare Projects", MenuAction::none}}},
             {"View Reports",
              {{"New Report", MenuAction::none},
               {"Dashboards", MenuAction::none},
               {"Resources", MenuAction::none},
               {"Costs", MenuAction::none},
               {"In Progress", MenuAction::none},
               {"Getting Started", MenuAction::none},
               {"Custom", MenuAction::none},
               {"Recent", MenuAction::none}}},
             {"Export", {{"Visual Reports", MenuAction::none}}},
         }},
        {"Project",
         {
             {"Insert", {{"Subproject", MenuAction::none}}},
             {"Add-ins",
              {{"Get Add-ins", MenuAction::none},
               {"My Add-ins", MenuAction::none}}},
             {"Properties",
              {{"Project Information", MenuAction::none},
               {"Custom Fields", MenuAction::none},
               {"Links Between Projects", MenuAction::none},
               {"WBS", MenuAction::none},
               {"Change Working Time", MenuAction::none}}},
             {"Schedule",
              {{"Calculate Project", MenuAction::none},
               {"Set Baseline", MenuAction::none},
               {"Move Project", MenuAction::none}}},
             {"Status",
              {{"Status Date", MenuAction::none},
               {"Update Project", MenuAction::none}}},
             {"Proofing", {{"Spelling", MenuAction::none}}},
         }},
        {"View",
         {
             {"Task Views",
              {{"Gantt Chart", MenuAction::none},
               {"Task Usage", MenuAction::view_task_usage},
               {"Network Diagram", MenuAction::none},
               {"Calendar", MenuAction::none},
               {"Other Views", MenuAction::none}}},
             {"Resource Views",
              {{"Team Planner", MenuAction::none},
               {"Resource Usage", MenuAction::view_resource_usage},
               {"Resource Sheet", MenuAction::view_resource_sheet},
               {"Other Views", MenuAction::none}}},
             {"Data",
              {{"Sort", MenuAction::none},
               {"Outline", MenuAction::none},
               {"Tables", MenuAction::none},
               // The capture stacks Highlight above Filter above Group by.
               {"Highlight", MenuAction::none},
               {"Filter", MenuAction::show_filter},
               {"Group by", MenuAction::none}}},
             {"Zoom",
              {{"Timescale", MenuAction::none},
               {"Zoom", MenuAction::none},
               {"Entire Project", MenuAction::none},
               {"Selected Tasks", MenuAction::none}}},
             {"Split View",
              {{"Timeline", MenuAction::none}, {"Details", MenuAction::none}}},
             {"Window",
              {{"New Window", MenuAction::none},
               {"Switch Windows", MenuAction::none},
               {"Arrange All", MenuAction::none},
               {"Hide", MenuAction::none}}},
             {"Macros", {{"Macros", MenuAction::none}}},
         }},
        {"Help",
         {
             {"Help",
              {{"Help", MenuAction::none},
               {"Feedback", MenuAction::none},
               {"Show Training", MenuAction::none}}},
         }},
        {"Gantt Chart Format",
         {
             {"Format",
              {{"Text Styles", MenuAction::none},
               {"Gridlines", MenuAction::none},
               {"Layout", MenuAction::none}}},
             {"Columns",
              {{"Insert Column", MenuAction::none},
               {"Column Settings", MenuAction::none},
               {"Custom Fields", MenuAction::none}}},
             {"Bar Styles",
              {{"Format", MenuAction::none},
               {"Critical Tasks", MenuAction::none},
               {"Slack", MenuAction::none},
               {"Late Tasks", MenuAction::none},
               {"Task Path", MenuAction::none},
               {"Baseline", MenuAction::none},
               {"Slippage", MenuAction::none}}},
             // 16 bar-colour swatches + preset drop-down: a palette, not text
             // commands, so the group carries its measured caption only.
             {"Gantt Chart Style", {}},
             {"Show/Hide",
              {{"Outline Number", MenuAction::none},
               {"Project Summary Task", MenuAction::none},
               {"Summary Tasks", MenuAction::none}}},
             {"Drawings", {{"Drawing", MenuAction::none}}},
         }},
    };
    return kPins;
}

QStringList titles_of(const std::vector<TabPin>& pins) {
    QStringList titles;
    for (const TabPin& pin : pins) {
        titles.push_back(QString::fromUtf8(pin.title));
    }
    return titles;
}

// The active tab's group captions from the live captions, in page order.  Also
// fills `boxes` with the group box that owns each caption.
QStringList live_groups(msqt::RibbonBar* ribbon, std::vector<QWidget*>* boxes) {
    QStringList titles;
    const QList<QLabel*> captions =
        ribbon->findChildren<QLabel*>(QStringLiteral("ribbonGroupLabel"));
    for (QLabel* caption : captions) {
        if (caption == nullptr) {
            continue;
        }
        titles.push_back(caption->text());
        if (boxes != nullptr) {
            boxes->push_back(caption->parentWidget());
        }
    }
    return titles;
}

// One tab's live inventory against its pin.  `tab_index` is used only to name
// the failures; the live data comes from the ribbon.
void check_inventory(msqt::RibbonBar* ribbon, const TabPin& pin, int tab_index) {
    const std::string where = std::string("tab ") + std::to_string(tab_index) + " (" +
                              pin.title + ")";
    const QStringList live_titles = ribbon->group_titles();
    QStringList pin_titles;
    for (const GroupPin& group : pin.groups) {
        pin_titles.push_back(QString::fromUtf8(group.title));
    }
    gate::check(live_titles == pin_titles, (where + ": group_titles is the pin").c_str());
    if (live_titles != pin_titles) {
        std::fprintf(stderr, "  live groups: %s\n  pin groups:  %s\n",
                     live_titles.join(QStringLiteral(" | ")).toUtf8().constData(),
                     pin_titles.join(QStringLiteral(" | ")).toUtf8().constData());
    }

    for (std::size_t group_index = 0; group_index < pin.groups.size(); ++group_index) {
        const GroupPin& group = pin.groups[group_index];
        const QStringList labels = ribbon->group_button_labels(static_cast<int>(group_index));
        QStringList pin_labels;
        for (const ButtonPin& button : group.buttons) {
            pin_labels.push_back(QString::fromUtf8(button.label));
        }
        gate::check(labels == pin_labels,
                    (where + " / " + group.title + ": button labels and order are the pin")
                        .c_str());
        if (labels != pin_labels) {
            std::fprintf(stderr, "  live buttons: %s\n  pin buttons:  %s\n",
                         labels.join(QStringLiteral(" | ")).toUtf8().constData(),
                         pin_labels.join(QStringLiteral(" | ")).toUtf8().constData());
        }
        for (std::size_t button_index = 0; button_index < group.buttons.size();
             ++button_index) {
            const ButtonPin& button = group.buttons[button_index];
            const MenuAction action = ribbon->group_button_action(
                static_cast<int>(group_index), static_cast<int>(button_index));
            gate::check(action == button.action,
                        (where + " / " + group.title + " / " + button.label +
                         ": kept MenuAction mapping")
                            .c_str());
        }
    }
}

// Every live button's decision-9 rule: enabled iff the shell implements the
// modelled action; a disabled button carries the exact product tooltip.
void check_button_state(msqt::RibbonBar* ribbon, const TabPin& pin) {
    for (std::size_t group_index = 0; group_index < pin.groups.size(); ++group_index) {
        const GroupPin& group = pin.groups[group_index];
        for (std::size_t button_index = 0; button_index < group.buttons.size();
             ++button_index) {
            const ButtonPin& button = group.buttons[button_index];
            const MenuAction action = ribbon->group_button_action(
                static_cast<int>(group_index), static_cast<int>(button_index));
            const bool implemented = msqt::shell_implements(action);
            // The group's box owns exactly its own buttons, in creation order.
            const QToolButton* live = nullptr;
            QWidget* box = nullptr;
            const QList<QLabel*> captions =
                ribbon->findChildren<QLabel*>(QStringLiteral("ribbonGroupLabel"));
            if (static_cast<int>(group_index) < captions.size()) {
                box = captions[static_cast<int>(group_index)]->parentWidget();
            }
            if (box != nullptr) {
                const QList<QToolButton*> owned =
                    box->findChildren<QToolButton*>(QStringLiteral("ribbonButton"));
                if (static_cast<int>(button_index) < owned.size()) {
                    live = owned[static_cast<int>(button_index)];
                }
            }
            if (live == nullptr) {
                gate::check(false, "the pinned button exists in the live page");
                continue;
            }
            const QString expected_tip =
                implemented ? QString::fromUtf8(button.label)
                            : QStringLiteral("not implemented in this port");
            gate::check(live->isEnabled() == implemented,
                        "a ribbon button is enabled iff the shell implements it");
            gate::check(live->toolTip() == expected_tip,
                        "a ribbon button carries its label or the exact disabled tooltip");
            gate::check(!live->text().isEmpty(), "a ribbon button has a label");
        }
    }
}

// Geometry for the active tab at the current ribbon width: visible groups are a
// prefix; visible group boxes and their captions/buttons are wholly inside the
// ribbon and never clip their text; the ">>" chevron is visible exactly when the
// inventory overflows.
void check_layout(msqt::RibbonBar* ribbon, const char* tab_title, bool* overflowed) {
    std::vector<QWidget*> boxes;
    const QStringList groups = live_groups(ribbon, &boxes);
    gate::check(!groups.isEmpty(), "the active tab has at least one group");

    bool hidden_seen = false;
    bool prefix_ok = true;
    bool visible_box_ok = true;
    bool caption_ok = true;
    int visible_groups = 0;
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        QWidget* box = boxes[index];
        if (box == nullptr) {
            continue;
        }
        const bool visible = box->isVisible();
        if (!visible) {
            hidden_seen = true;
            continue;
        }
        ++visible_groups;
        if (hidden_seen) {
            prefix_ok = false;
        }
        const QRect mapped(box->mapTo(ribbon, QPoint(0, 0)), box->size());
        if (!gate::inside(ribbon->rect(), mapped)) {
            visible_box_ok = false;
        }
        gate::check(box->width() >= box->sizeHint().width() - 1,
                    "a visible group keeps its measured width");
        for (QLabel* caption : box->findChildren<QLabel*>(
                 QStringLiteral("ribbonGroupLabel"))) {
            if (caption->fontMetrics().horizontalAdvance(caption->text()) >
                caption->width() + 1) {
                caption_ok = false;
            }
        }
    }
    gate::check(prefix_ok, "visible ribbon groups are a prefix of the inventory");
    gate::check(visible_box_ok,
                "every visible group box is wholly inside the ribbon");
    gate::check(caption_ok, "no visible group caption is clipped");

    int visible_buttons = 0;
    for (QToolButton* button :
         ribbon->findChildren<QToolButton*>(QStringLiteral("ribbonButton"))) {
        if (button == nullptr || !button->isVisible()) {
            continue;
        }
        ++visible_buttons;
        const int label_width = button->fontMetrics().horizontalAdvance(button->text());
        gate::check(button->width() >= label_width + 2,
                    "a visible ribbon button fits its whole label");
        gate::check(button->height() == msqt::kRibbonButtonHeight,
                    "a visible ribbon button keeps the measured height");
        const QRect mapped(button->mapTo(ribbon, QPoint(0, 0)), button->size());
        gate::check(gate::inside(ribbon->rect(), mapped),
                    "a visible ribbon button is wholly inside the ribbon");
    }
    gate::check(visible_groups > 0, "at least one whole group is visible");
    gate::check(visible_buttons > 0 || groups.size() == 1,
                "a visible tab with buttons shows at least one");

    const int total = static_cast<int>(boxes.size());
    const bool overflow = visible_groups < total;
    gate::check(ribbon->groups_overflow() == overflow,
                "groups_overflow() matches the visible inventory");
    gate::check(ribbon->overflow_button() != nullptr &&
                    ribbon->overflow_button()->isVisible() == overflow,
                "the >> chevron is visible exactly when the inventory overflows");
    if (overflow) {
        const QRect chevron(ribbon->overflow_button()->mapTo(ribbon, QPoint(0, 0)),
                            ribbon->overflow_button()->size());
        gate::check(gate::inside(ribbon->rect(), chevron),
                    "the >> chevron sits inside the ribbon");
    }
    if (overflowed != nullptr) {
        *overflowed = overflow;
    }
    std::fprintf(stderr, "note: %s at %d px: groups %d/%d, buttons %d, chevron %s\n",
                 tab_title, ribbon->width(), visible_groups, total, visible_buttons,
                 overflow ? "shown" : "hidden");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: ribbon_inventory_tests <fixture.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    msqt::RibbonBar* ribbon = window.ribbon();
    gate::check(ribbon != nullptr, "the window owns a ribbon");
    if (ribbon == nullptr) {
        return 1;
    }

    const std::vector<TabPin>& pins = pinned_tabs();
    gate::check(static_cast<int>(pins.size()) == 7, "the pin has seven command tabs");
    gate::check(ribbon->tab_count() == 7, "the ribbon keeps its seven command tabs");
    gate::check(ribbon->tab_titles() == titles_of(pins),
                "tab_titles() is exactly the seven measured captions, in order");

    // --- every tab's inventory and 1920 layout -----------------------------
    int overflow_tabs_1920 = 0;
    bool task_overflow_1920 = true;
    for (std::size_t tab_index = 0; tab_index < pins.size(); ++tab_index) {
        const TabPin& pin = pins[tab_index];
        window.resize(msqt::kParityWidth, msqt::kParityHeight);
        ribbon->set_active_tab(static_cast<int>(tab_index));
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(ribbon->active_tab_index() == static_cast<int>(tab_index),
                    "set_active_tab selects the tab (and checks its tab button)");
        check_inventory(ribbon, pin, static_cast<int>(tab_index));
        check_button_state(ribbon, pin);
        bool overflowed = false;
        check_layout(ribbon, pin.title, &overflowed);
        if (overflowed) {
            ++overflow_tabs_1920;
        }
        if (tab_index == 0) {
            task_overflow_1920 = overflowed;
        }
    }
    // "All visible at 1920" is NOT the criterion: each tab's chevron is checked
    // against ITS OWN overflow above, and the 900 px pass below proves the
    // chevron path works when the inventory really does not fit.  The Task tab
    // MUST fit: qt_empty_state and the 9/9 parity fixture measure it at 1920
    // with every group and button visible.
    std::fprintf(stderr,
                 "note: %d of %d tabs overflow at 1920 (text-button model; the "
                 "measured inventory is the criterion)\n",
                 overflow_tabs_1920, static_cast<int>(pins.size()));
    gate::check(!task_overflow_1920,
                "the Task tab still fits 1920 (the parity fixture's ribbon)");

    // --- the same contract at 900x560 --------------------------------------
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QApplication::processEvents();
    QApplication::processEvents();
    int overflow_tabs_900 = 0;
    for (std::size_t tab_index = 0; tab_index < pins.size(); ++tab_index) {
        ribbon->set_active_tab(static_cast<int>(tab_index));
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(ribbon->tab_titles() == titles_of(pins),
                    "narrowing the window never changes the tab model");
        bool overflowed = false;
        check_layout(ribbon, pins[tab_index].title, &overflowed);
        if (overflowed) {
            ++overflow_tabs_900;
        }
    }
    gate::check(overflow_tabs_900 >= 1,
                "at 900 at least one tab shows the >> chevron");

    // --- the contextual tab exists only in the Gantt view ------------------
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    QApplication::processEvents();
    ribbon->set_active_tab(0);
    QApplication::processEvents();

    QString error;
    const bool opened = window.open_file(fixture, &error);
    gate::check(opened, "the committed fixture opens");
    if (opened) {
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(ribbon->tab_count() == 7,
                    "the loaded Gantt view keeps the seven-tab model");
        gate::check(ribbon->contextual_tab() != nullptr &&
                        ribbon->contextual_tab()->isVisible(),
                    "the Gantt view shows the contextual Gantt Chart Format tab");

        QAction* resource_sheet = gate::find_action(window, MenuAction::view_resource_sheet);
        gate::check(resource_sheet != nullptr, "the Resource Sheet view action exists");
        if (resource_sheet != nullptr) {
            resource_sheet->trigger();
            QApplication::processEvents();
            gate::check(window.view_mode() == app::ViewMode::resource_sheet,
                        "the Resource Sheet view is active");
            gate::check(ribbon->tab_count() == 7,
                        "a grid view keeps the seven-tab model (only the contextual tab hides)");
            gate::check(ribbon->contextual_tab() != nullptr &&
                            !ribbon->contextual_tab()->isVisible(),
                        "a grid view hides the contextual Gantt Chart Format tab");
        }

        QAction* task_sheet = gate::find_action(window, MenuAction::view_task_sheet);
        gate::check(task_sheet != nullptr, "the Task Sheet view action exists");
        if (task_sheet != nullptr) {
            task_sheet->trigger();
            QApplication::processEvents();
            gate::check(window.view_mode() == app::ViewMode::task_sheet,
                        "the Gantt (task sheet) view is active again");
            gate::check(ribbon->tab_count() == 7,
                        "returning to the Gantt view keeps the seven-tab model");
            gate::check(ribbon->contextual_tab() != nullptr &&
                            ribbon->contextual_tab()->isVisible(),
                        "the Gantt view shows the contextual tab again");
        }
    }

    std::printf("qt_ribbon_inventory: ");
    return gate::finish();
}
