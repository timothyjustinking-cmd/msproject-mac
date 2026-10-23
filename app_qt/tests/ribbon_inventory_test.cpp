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
#include <QFile>
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
    std::string label;
    std::string kind;  // wave 24 (W24-12/R15): the pin's kind column
    bool enabled = false;
};

struct GroupPin {
    std::string title;
    std::vector<ButtonPin> buttons;
};

struct TabPin {
    std::string title;
    bool contextual = false;
    std::vector<GroupPin> groups;
};

// ---------------------------------------------------------------------------
// THE PIN (wave 23, A-5): a COMMITTED, independent file typed from Fable's
// re-read of the strips (docs/handoff/audit-w22/A2-fable-readback.md), not a
// second copy of the implementation.  The gate parses it; the builder is the
// implementation under test.  The `enabled` column is the pinned decision-9
// set, so "enabled iff implemented" is no longer the test's own premise.
//
// TSV: tab, contextual(0/1), group, kind, label, enabled(0/1), icon key.
// A `group` row opens a group; `button` rows follow in order.

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (const char c : line) {
        if (c == '\t') {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(c);
        }
    }
    fields.push_back(field);
    return fields;
}

std::vector<TabPin> load_pin(const QString& path) {
    QFile file(path);
    std::vector<TabPin> pins;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return pins;
    }
    while (!file.atEnd()) {
        const std::string raw = file.readLine().toStdString();
        if (raw.empty() || raw[0] == '#') {
            continue;
        }
        std::string line = raw;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        const std::vector<std::string> fields = split_tabs(line);
        if (fields.size() < 7) {
            continue;
        }
        if (pins.empty() || pins.back().title != fields[0]) {
            TabPin tab;
            tab.title = fields[0];
            tab.contextual = fields[1] == "1";
            pins.push_back(tab);
        }
        TabPin& tab = pins.back();
        if (fields[3] == "group") {
            GroupPin group;
            group.title = fields[2];
            tab.groups.push_back(group);
        } else if (!tab.groups.empty()) {
            ButtonPin button;
            button.label = fields[4];
            button.kind = fields[3];
            button.enabled = fields[5] == "1";
            tab.groups.back().buttons.push_back(button);
        }
    }
    return pins;
}

QStringList titles_of(const std::vector<TabPin>& pins) {
    QStringList titles;
    for (const TabPin& pin : pins) {
        titles.push_back(QString::fromUtf8(pin.title.c_str()));
    }
    return titles;
}

// The pin kinds this gate models: only a plain button.  Everything else (combo,
// checkbox, gallery, dialog launcher) lives in ribbon_pin_unmodelled.tsv and is
// not built; if one appears in the modelled pin, the gate must fail rather than
// treat it as a button.
bool pin_kind_is_button(const std::string& kind) { return kind == "button"; }

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
        pin_titles.push_back(QString::fromUtf8(group.title.c_str()));
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
            pin_labels.push_back(QString::fromUtf8(button.label.c_str()));
            // Wave 24 (W24-12/R15): the pin's KIND column is asserted.  The
            // live inventory this gate compares is a list of text buttons; a
            // pin row for a combo/checkbox/gallery must not silently pass as
            // one.  A combo built as a button therefore turns this red.
            gate::check(pin_kind_is_button(button.kind),
                        (where + " / " + group.title + ": pin kind '" + button.kind +
                         "' is a button control")
                            .c_str());
        }
        gate::check(labels == pin_labels,
                    (where + " / " + group.title + ": button labels and order are the pin")
                        .c_str());
        if (labels != pin_labels) {
            std::fprintf(stderr, "  live buttons: %s\n  pin buttons:  %s\n",
                         labels.join(QStringLiteral(" | ")).toUtf8().constData(),
                         pin_labels.join(QStringLiteral(" | ")).toUtf8().constData());
        }
    }
}

// Wave 23 review finding: the independent pin carries the enabled SET but not
// the command each button runs, so dropping the old MenuAction assertion left a
// mis-wired button undetected.  This table pins the action of every ENABLED
// control (the implemented commands); disabled controls cannot act.
struct EnabledWiring {
    const char* tab;
    const char* group;
    const char* label;
    const char* action;
};
const EnabledWiring kEnabledWiring[] = {
    {"Task", "Clipboard", "Paste", "paste"},
    {"Task", "Clipboard", "Cut", "cut"},
    {"Task", "Clipboard", "Copy", "copy"},
    {"Task", "Insert", "Task", "insert_task"},
    {"Task", "Properties", "Information", "task_fields"},
    {"Task", "Editing", "Find", "show_filter"},
    {"Task", "Editing", "Clear", "clear_filter"},
    {"View", "Task Views", "Task Usage", "view_task_usage"},
    {"View", "Resource Views", "Resource Usage", "view_resource_usage"},
    {"View", "Resource Views", "Resource Sheet", "view_resource_sheet"},
    {"View", "Data", "Filter", "show_filter"},
    {"Project", "Properties", "Custom Fields", "custom_fields"},
    {"Gantt Chart Format", "Columns", "Custom Fields", "custom_fields"},
};

void check_action_wiring(msqt::RibbonBar* ribbon, const TabPin& pin) {
    for (const EnabledWiring& wiring : kEnabledWiring) {
        if (pin.title != wiring.tab) {
            continue;
        }
        int group_index = -1;
        int button_index = -1;
        for (std::size_t g = 0; g < pin.groups.size(); ++g) {
            if (pin.groups[g].title != wiring.group) {
                continue;
            }
            group_index = static_cast<int>(g);
            for (std::size_t b = 0; b < pin.groups[g].buttons.size(); ++b) {
                if (pin.groups[g].buttons[b].label == wiring.label) {
                    button_index = static_cast<int>(b);
                    break;
                }
            }
            break;
        }
        if (group_index < 0 || button_index < 0) {
            gate::check(false, "an enabled control exists in the pin for its wiring");
            continue;
        }
        const app::MenuAction action =
            ribbon->group_button_action(group_index, button_index);
        const std::string named = std::string("ribbon button '") + wiring.label +
                                  "' runs " + wiring.action;
        gate::check(std::string(app::menu_action_name(action)) == wiring.action,
                    named.c_str());
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
            // Wave 23: the ENABLED SET is the pin's, counted and named -- the
            // wave-22 gate asked the implementation which actions it implements.
            const bool implemented = button.enabled;
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
                implemented ? QString::fromUtf8(button.label.c_str())
                            : QStringLiteral("not implemented in this port");
            const std::string named =
                std::string("ribbon button '") + button.label +
                "' enabled state is the independent pin's";
            gate::check(live->isEnabled() == implemented, named.c_str());
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

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: ribbon_inventory_tests <fixture.mpp> <ribbon_pin.tsv>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);

    // Wave 24 (W24-12/R15): prove the kind predicate itself -- a combo must not
    // be accepted as a button, or the pin's kind column would be decorative.
    gate::check(pin_kind_is_button("button"), "a button pin is a button");
    gate::check(!pin_kind_is_button("combo"), "a combo pin is not a button");
    gate::check(!pin_kind_is_button("checkbox"), "a checkbox pin is not a button");
    gate::check(!pin_kind_is_button(""), "an empty kind is not a button");

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

    // Wave 23 (A-5): the enabled set is the product's state with a plan open,
    // not the empty-window state (Paste/Cut/Copy/Find need a document).
    QString open_error;
    const bool opened_for_state = window.open_file(fixture, &open_error);
    gate::check(opened_for_state, "the committed fixture opens for the enabled set");
    QApplication::processEvents();

    const std::vector<TabPin> pins = load_pin(QString::fromLocal8Bit(argv[2]));
    gate::check(static_cast<int>(pins.size()) == 7, "the pin has seven command tabs");
    if (pins.size() != 7) {
        std::printf("qt_ribbon_inventory: ");
        return gate::finish();
    }
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
        check_action_wiring(ribbon, pin);
        check_button_state(ribbon, pin);
        bool overflowed = false;
        check_layout(ribbon, pin.title.c_str(), &overflowed);
        if (overflowed) {
            ++overflow_tabs_1920;
        }
        if (tab_index == 0) {
            task_overflow_1920 = overflowed;
        }
    }
    // Wave 23 (A-5): at the reference width EVERY tab must show its LAST group
    // -- a trailing group silently dropped was the wave-22 hole.  The 900 pass
    // below proves the chevron path when the inventory really does not fit.
    std::fprintf(stderr, "note: %d of %d tabs overflow at 1920\n", overflow_tabs_1920,
                 static_cast<int>(pins.size()));
    gate::check(overflow_tabs_1920 == 0,
                "at 1920 every tab shows its last group (nothing clipped)");
    gate::check(!task_overflow_1920, "the Task tab still fits 1920");

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
        // Wave 23 (A-5): the 900 pass re-checks the INVENTORY, not only the
        // layout -- the wave-22 hole was an inventory that stopped at the
        // first clipped group.
        check_inventory(ribbon, pins[tab_index], static_cast<int>(tab_index));
        check_button_state(ribbon, pins[tab_index]);
        bool overflowed = false;
        check_layout(ribbon, pins[tab_index].title.c_str(), &overflowed);
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
