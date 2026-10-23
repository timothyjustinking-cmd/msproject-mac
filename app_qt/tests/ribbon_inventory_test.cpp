// app_qt/tests/ribbon_inventory_test.cpp -- the wave-22 inventory gate, grown by
// wave 26 (G1) to every control kind Project's clean strips show.
//
// The source of truth is the local, git-ignored reference set:
//   * the clean ribbon strips under corpus/reference/2026-10-23-msproject/
//     (project-ribbon-{resource,report,project,view,help,gantt-format}.png, the
//     Task tab from project-gantt-loaded-clean.png);
//   * the independent pin app_qt/ribbon_pin.tsv, typed from Fable's re-read
//     (docs/handoff/audit-w22/A2-fable-readback.md);
//   * the measured numbers in tests/data/reference/w26-ribbon.measures.json
//     (group rectangles, stack row baselines, the 16-swatch palette).
//
// What this gate asserts:
//   * the pin has seven tabs and app_qt/ribbon_pin_unmodelled.tsv is empty;
//   * for EVERY tab: group_titles() and each group's controls (kind, label,
//     order, enabled state, exact disabled tooltip, state) are the pin's;
//   * the kind comparison is pin-kind vs the LIVE WIDGET CLASS
//     (RibbonBar::control_kind) -- never the port's spec;
//   * at 1920 every tab shows all its groups, each group box matches the
//     measured clean-strip rectangle (+/- 2 px), every control lies inside its
//     group, and every stack control sits on the measured row baseline
//     (5 + 22*row) with its row peers;
//   * at 900 the prefix/fit/chevron contract holds;
//   * the contextual Gantt Chart Format tab is shown only in the Gantt view;
//   * the enabled controls are wired: the pin's enabled set matches the live
//     enabled state and the action/command each one runs, and the functional
//     controls change the window (Timeline strip, Project Summary row, Critical
//     Tasks highlight, % Complete on the selection, Unlink);
//   * the gallery's 16 swatches carry the measured palette colours.
//
// The fixtures the functional checks open are copies of committed files; nothing
// here writes tests/data (all opens are read-only through PlanDocument).
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateEdit>
#include <QFile>
#include <QFontMetrics>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "command_status.h"
#include "gate_util.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "selection_model.h"
#include "shell_style.h"
#include "timeline_strip.h"

namespace {

using app::MenuAction;
using msqt::RibbonCommand;

struct ControlPin {
    std::string label;
    std::string kind;
    std::string icon;
    std::string state;
    bool enabled = false;
    int row = -1;
};

struct GroupPin {
    std::string title;
    int left = 0;
    int right = 0;
    std::vector<ControlPin> controls;
};

struct TabPin {
    std::string title;
    bool contextual = false;
    std::vector<GroupPin> groups;
};

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

// ---------------------------------------------------------------------------
// The pin: tab, contextual, group, kind, label, enabled, icon, row, state.
// ---------------------------------------------------------------------------
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
        if (fields.size() < 9) {
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
            ControlPin control;
            control.kind = fields[3];
            control.label = fields[4];
            control.enabled = fields[5] == "1";
            control.icon = fields[6];
            control.row = std::atoi(fields[7].c_str());
            control.state = fields[8];
            tab.groups.back().controls.push_back(control);
        }
    }
    return pins;
}

// The measured clean-strip rectangles ("tabs" -> tab -> groups).
struct MeasuredGroup {
    std::string title;
    int left = 0;
    int right = 0;
};

// The per-tab measurements live under "tabs" -> tab -> groups.
std::vector<MeasuredGroup> measured_groups(const QJsonObject& root, const std::string& tab) {
    std::vector<MeasuredGroup> out;
    const QJsonObject tabs = root.value(QStringLiteral("tabs")).toObject();
    const QJsonArray groups =
        tabs.value(QString::fromUtf8(tab.c_str())).toObject()
            .value(QStringLiteral("groups")).toArray();
    for (const QJsonValue& value : groups) {
        const QJsonObject group = value.toObject();
        MeasuredGroup measured;
        measured.title = group.value(QStringLiteral("title")).toString().toStdString();
        measured.left = group.value(QStringLiteral("left")).toInt();
        measured.right = group.value(QStringLiteral("right")).toInt();
        out.push_back(std::move(measured));
    }
    return out;
}

QString normalized(const QString& text) {
    QString out = text;
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return out.simplified();
}

QString live_label(const QWidget* control) {
    if (control == nullptr) {
        return QString();
    }
    if (const auto* button = qobject_cast<const QToolButton*>(control)) {
        return button->text().isEmpty() ? button->accessibleName() : button->text();
    }
    if (const auto* box = qobject_cast<const QCheckBox*>(control)) {
        return box->text();
    }
    // Combos, the gallery, the date field and launchers carry the pin's label as
    // their accessible name (a combo's text is its VALUE, not its label).
    return control->accessibleName();
}

QString live_state(const QWidget* control) {
    if (control == nullptr) {
        return QStringLiteral("-");
    }
    if (const auto* box = qobject_cast<const QCheckBox*>(control)) {
        return box->isChecked() ? QStringLiteral("checked") : QStringLiteral("unchecked");
    }
    if (const auto* combo = qobject_cast<const QComboBox*>(control)) {
        return combo->currentText();
    }
    if (control->objectName() == QStringLiteral("ribbonGallery")) {
        return QString::number(control->findChildren<QToolButton*>(
                                   QStringLiteral("ribbonSwatch"))
                                   .size());
    }
    if (control->objectName() == QStringLiteral("ribbonField")) {
        const QList<QDateEdit*> edits = control->findChildren<QDateEdit*>();
        if (!edits.isEmpty()) {
            return edits.first()->text();
        }
    }
    return QStringLiteral("-");
}

// One tab's live inventory against its pin, plus the measured geometry.
void check_inventory(msqt::RibbonBar* ribbon, const TabPin& pin, const QJsonObject& measures,
                     int tab_index, int top_offset, int row_height) {
    const std::string where =
        std::string("tab ") + std::to_string(tab_index) + " (" + pin.title + ")";
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

    const std::vector<MeasuredGroup> measured = measured_groups(measures, pin.title);
    gate::check(measured.size() == pin.groups.size(),
                (where + ": the measured strip has the pin's groups").c_str());

    // A widget's top in the group box's own coordinates.  mapToGlobal is used
    // rather than QWidget::mapTo because the group box is not required to be a
    // Qt ancestor of the control (the caption's label is a sibling branch).
    auto relative_top = [](const QWidget* control, const QWidget* box) {
        return control->mapToGlobal(QPoint(0, 0)).y() - box->mapToGlobal(QPoint(0, 0)).y();
    };
    auto relative_rect = [](const QWidget* control, const QWidget* box) {
        const QPoint origin = control->mapToGlobal(QPoint(0, 0)) -
                              box->mapToGlobal(QPoint(0, 0));
        return QRect(origin, control->size());
    };
    for (std::size_t group_index = 0; group_index < pin.groups.size(); ++group_index) {
        const GroupPin& group = pin.groups[group_index];
        QWidget* box = ribbon->group_box(static_cast<int>(group_index));
        const int controls = ribbon->group_control_count(static_cast<int>(group_index));
        gate::check(controls == static_cast<int>(group.controls.size()),
                    (where + " / " + group.title + ": the control count is the pin").c_str());
        for (std::size_t control_index = 0; control_index < group.controls.size();
             ++control_index) {
            const ControlPin& expected = group.controls[control_index];
            QWidget* live =
                ribbon->group_control(static_cast<int>(group_index),
                                      static_cast<int>(control_index));
            if (live == nullptr) {
                gate::check(false, "a pinned control exists in the live page");
                continue;
            }
            const QString live_kind = msqt::RibbonBar::control_kind(live);
            gate::check(live_kind == QString::fromUtf8(expected.kind.c_str()),
                        (where + " / " + group.title + " / " + expected.label +
                         ": pin kind '" + expected.kind + "' is the live widget class ('" +
                         live_kind.toStdString() + "')")
                            .c_str());
            const QString label = normalized(live_label(live));
            gate::check(label == normalized(QString::fromUtf8(expected.label.c_str())),
                        (where + " / " + group.title + ": control label and order are the pin")
                            .c_str());
            if (label != normalized(QString::fromUtf8(expected.label.c_str()))) {
                std::fprintf(stderr, "  live: '%s'  pin: '%s'\n",
                             label.toUtf8().constData(), expected.label.c_str());
            }
            gate::check(live->isEnabled() == expected.enabled,
                        (where + " / " + group.title + " / " + expected.label +
                         ": enabled state is the independent pin's")
                            .c_str());
            const QString tip = live->toolTip();
            if (expected.enabled) {
                gate::check(!tip.isEmpty(),
                            "an enabled ribbon control carries a tooltip");
            } else {
                gate::check(tip == QStringLiteral("not implemented in this port"),
                            "a disabled ribbon control carries the exact decision-9 tooltip");
            }
            // State: check boxes (except file-driven ones), combos, the gallery
            // and the date field.
            if (expected.state != "-") {
                if (expected.kind == "checkbox" && expected.state == "file") {
                    // The functional section proves the file-driven initial
                    // state and the toggle against the real fixtures.
                } else {
                    const QString state = live_state(live);
                    gate::check(state == QString::fromUtf8(expected.state.c_str()),
                                (where + " / " + group.title + " / " + expected.label +
                                 ": live state is the pin's")
                                    .c_str());
                    if (state != QString::fromUtf8(expected.state.c_str())) {
                        std::fprintf(stderr, "  live state '%s'  pin state '%s'\n",
                                     state.toUtf8().constData(), expected.state.c_str());
                    }
                }
            }
            // Stack geometry: a one-row control must sit on the measured row
            // baseline, share its row's top, and stay inside its group.  The
            // launcher and the gallery are not stack rows (their pin row is the
            // group's bottom row for reference only).
            if (expected.row >= 0 && expected.kind != "launcher" &&
                expected.kind != "gallery" && live->height() <= row_height + 2 &&
                box != nullptr) {
                const int top = relative_top(live, box);
                const int want = top_offset + expected.row * row_height;
                gate::check(std::abs(top - want) <= 2,
                            (where + " / " + group.title + " / " + expected.label +
                             ": stack row sits on the measured baseline")
                                .c_str());
                if (std::abs(top - want) > 2) {
                    std::fprintf(stderr, "  top %d, want %d\n", top, want);
                }
                gate::check(gate::inside(box->rect(), relative_rect(live, box)),
                            "a ribbon control lies inside its group box");
                // Row peers share a top (+/- 1 px).
                for (std::size_t peer_index = 0; peer_index < group.controls.size();
                     ++peer_index) {
                    const ControlPin& peer = group.controls[peer_index];
                    if (peer_index == control_index || peer.row != expected.row ||
                        peer.kind == "button" || peer.kind == "split") {
                        continue;
                    }
                    QWidget* peer_live =
                        ribbon->group_control(static_cast<int>(group_index),
                                              static_cast<int>(peer_index));
                    if (peer_live == nullptr || peer_live->height() > row_height + 2) {
                        continue;
                    }
                    gate::check(std::abs(relative_top(peer_live, box) - top) <= 1,
                                "controls on one stack row share a top");
                }
            }
        }
        if (group_index < static_cast<std::size_t>(measured.size()) && box != nullptr) {
            const int left = box->mapToGlobal(QPoint(0, 0)).x() -
                             ribbon->mapToGlobal(QPoint(0, 0)).x();
            const int right = left + box->width() + 1;
            gate::check(std::abs(left - measured[group_index].left) <= 2,
                        (where + " / " + group.title +
                         ": group left edge is the measured clean-strip rectangle")
                            .c_str());
            gate::check(std::abs(right - measured[group_index].right) <= 2,
                        (where + " / " + group.title +
                         ": group right edge is the measured clean-strip rectangle")
                            .c_str());
            if (std::abs(left - measured[group_index].left) > 2 ||
                std::abs(right - measured[group_index].right) > 2) {
                std::fprintf(stderr, "  group '%s': live [%d,%d] measured [%d,%d]\n",
                             group.title.c_str(), left, right,
                             measured[group_index].left, measured[group_index].right);
            }
        }
    }
}

// Geometry for the active tab at the current ribbon width: visible groups are a
// prefix; visible group boxes and captions are wholly inside the ribbon and
// never clip; the ">>" chevron is visible exactly when the inventory overflows.
void check_layout(msqt::RibbonBar* ribbon, const char* tab_title, bool* overflowed) {
    std::vector<QWidget*> boxes;
    const QStringList groups = ribbon->group_titles();
    for (int index = 0; index < groups.size(); ++index) {
        boxes.push_back(ribbon->group_box(index));
    }
    gate::check(!groups.isEmpty(), "the active tab has at least one group");

    bool hidden_seen = false;
    bool prefix_ok = true;
    bool visible_box_ok = true;
    bool caption_ok = true;
    int visible_groups = 0;
    for (QWidget* box : boxes) {
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
        gate::check(box->width() == box->minimumWidth() ||
                        box->width() >= box->sizeHint().width() - 1,
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
        int label_width = 0;
        for (const QString& line : button->text().split(QLatin1Char('\n'))) {
            label_width = std::max(label_width,
                                   button->fontMetrics().horizontalAdvance(line));
        }
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

// The pin's enabled set, pinned by command: dropping a wiring must fail.
struct EnabledWiring {
    const char* tab;
    const char* group;
    const char* label;
    const char* action;   // MenuAction name, or "" when `command` is used
    const char* command;  // RibbonCommand name, or ""
};
const EnabledWiring kEnabledWiring[] = {
    {"Task", "Clipboard", "Paste", "paste", ""},
    {"Task", "Clipboard", "Cut", "cut", ""},
    {"Task", "Clipboard", "Copy", "copy", ""},
    {"Task", "Insert", "Task", "insert_task", ""},
    {"Task", "Properties", "Information", "task_fields", ""},
    {"Task", "Editing", "Find", "show_filter", ""},
    {"Task", "Editing", "Clear", "clear_filter", ""},
    {"View", "Task Views", "Task Usage", "view_task_usage", ""},
    {"View", "Resource Views", "Resource Usage", "view_resource_usage", ""},
    {"View", "Resource Views", "Resource Sheet", "view_resource_sheet", ""},
    {"Project", "Properties", "Custom Fields", "custom_fields", ""},
    {"Project", "Properties", "Project Information", "", "project_information"},
    {"Gantt Chart Format", "Columns", "Custom Fields", "custom_fields", ""},
    {"Task", "Schedule", "0%", "", "percent_0"},
    {"Task", "Schedule", "25%", "", "percent_25"},
    {"Task", "Schedule", "50%", "", "percent_50"},
    {"Task", "Schedule", "75%", "", "percent_75"},
    {"Task", "Schedule", "100%", "", "percent_100"},
    {"Task", "Schedule", "link tasks", "", "link_tasks"},
    {"Task", "Schedule", "unlink tasks", "", "unlink_tasks"},
    {"View", "Split View", "Timeline", "", "toggle_timeline"},
    {"Gantt Chart Format", "Bar Styles", "Critical Tasks", "", "toggle_critical_tasks"},
    {"Gantt Chart Format", "Show/Hide", "Project Summary Task", "",
     "toggle_project_summary"},
};

const char* command_name(RibbonCommand command) {
    switch (command) {
        case RibbonCommand::none: return "none";
        case RibbonCommand::percent_0: return "percent_0";
        case RibbonCommand::percent_25: return "percent_25";
        case RibbonCommand::percent_50: return "percent_50";
        case RibbonCommand::percent_75: return "percent_75";
        case RibbonCommand::percent_100: return "percent_100";
        case RibbonCommand::link_tasks: return "link_tasks";
        case RibbonCommand::unlink_tasks: return "unlink_tasks";
        case RibbonCommand::outdent: return "outdent";
        case RibbonCommand::indent: return "indent";
        case RibbonCommand::split_task: return "split_task";
        case RibbonCommand::toggle_critical_tasks: return "toggle_critical_tasks";
        case RibbonCommand::toggle_project_summary: return "toggle_project_summary";
        case RibbonCommand::toggle_timeline: return "toggle_timeline";
        case RibbonCommand::toggle_details: return "toggle_details";
        case RibbonCommand::project_information: return "project_information";
        case RibbonCommand::change_working_time: return "change_working_time";
        case RibbonCommand::links_between_projects: return "links_between_projects";
        case RibbonCommand::update_project: return "update_project";
        case RibbonCommand::move_project: return "move_project";
        case RibbonCommand::wbs_menu: return "wbs_menu";
        case RibbonCommand::set_baseline_menu: return "set_baseline_menu";
    }
    return "none";
}

void check_action_wiring(msqt::RibbonBar* ribbon, const TabPin& pin) {
    for (const EnabledWiring& wiring : kEnabledWiring) {
        if (pin.title != wiring.tab) {
            continue;
        }
        int group_index = -1;
        int control_index = -1;
        for (std::size_t g = 0; g < pin.groups.size(); ++g) {
            if (pin.groups[g].title != wiring.group) {
                continue;
            }
            group_index = static_cast<int>(g);
            for (std::size_t c = 0; c < pin.groups[g].controls.size(); ++c) {
                if (pin.groups[g].controls[c].label == wiring.label) {
                    control_index = static_cast<int>(c);
                    break;
                }
            }
            break;
        }
        if (group_index < 0 || control_index < 0) {
            gate::check(false, "an enabled control exists in the pin for its wiring");
            continue;
        }
        if (wiring.action[0] != '\0') {
            const app::MenuAction action =
                ribbon->group_control_action(group_index, control_index);
            gate::check(std::string(app::menu_action_name(action)) == wiring.action,
                        (std::string("ribbon control '") + wiring.label + "' runs " +
                         wiring.action)
                            .c_str());
        } else {
            const RibbonCommand command =
                ribbon->group_control_command(group_index, control_index);
            gate::check(std::string(command_name(command)) == wiring.command,
                        (std::string("ribbon control '") + wiring.label + "' runs " +
                         wiring.command)
                            .c_str());
        }
    }
}

// Find one live control by pin coordinates.
[[maybe_unused]] QWidget* find_control(msqt::RibbonBar* ribbon, const QString& tab,
                                       const QString& group, const QString& label) {
    const QStringList tabs = ribbon->tab_titles();
    const int tab_index = tabs.indexOf(tab);
    if (tab_index < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(tab_index);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int group_index = groups.indexOf(group);
    if (group_index < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group_index); ++control) {
        QWidget* live = ribbon->group_control(group_index, control);
        if (live != nullptr && normalized(live_label(live)) == normalized(label)) {
            return live;
        }
    }
    return nullptr;
}

// Press a live ribbon control the way a user does: QAbstractButton::click()
// emits the same clicked/toggled signals a mouse release does (and does nothing
// when the control is disabled, which is exactly the decision-9 contract).
[[maybe_unused]] bool click_control(QWidget* control) {
    auto* button = qobject_cast<QAbstractButton*>(control);
    if (button == nullptr) {
        return false;
    }
    button->click();
    return true;
}

QWidget* find_by_accessibility(msqt::RibbonBar* ribbon, const QString& tab,
                               const QString& accessible_name) {    const QStringList tabs = ribbon->tab_titles();
    const int tab_index = tabs.indexOf(tab);
    if (tab_index < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(tab_index);
    QApplication::processEvents();
    for (int group = 0; group < ribbon->group_titles().size(); ++group) {
        for (int control = 0; control < ribbon->group_control_count(group); ++control) {
            QWidget* live = ribbon->group_control(group, control);
            if (live != nullptr && live->accessibleName() == accessible_name) {
                return live;
            }
        }
    }
    return nullptr;
}

// Selects the RecordSet row whose uid is `uid` on the Name column (or column 0).
[[maybe_unused]] bool select_uid(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return false;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        if (sheet->row_uid(row) != uid) {
            continue;
        }
        std::size_t column = 0;
        for (std::size_t candidate = 0; candidate < sheet->column_count(); ++candidate) {
            if (sheet->column(candidate).field == app::name_field_id()) {
                column = candidate;
                break;
            }
        }
        return app::select_cell(*sheet, row, column);
    }
    return false;
}

[[maybe_unused]] const app::TaskRow* plan_task(const msqt::MainWindow& window,
                                              std::uint32_t uid) {
    for (const app::TaskRow& task : window.plan().tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 11) {
        std::fprintf(stderr,
                     "usage: ribbon_inventory_tests <fixture.mpp> <ribbon_pin.tsv> "
                     "<w26-ribbon.measures.json> <timeline-hidden.mpp> <timeline-shown.mpp> "
                     "<summary-on.mpp> <summary-off.mpp> <order.mpp> <outline.mpp> "
                     "<unmodelled.tsv> [corpus.mpp]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString pin_path = QString::fromLocal8Bit(argv[2]);
    const QString measures_path = QString::fromLocal8Bit(argv[3]);
    const QString timeline_hidden = QString::fromLocal8Bit(argv[4]);
    const QString timeline_shown = QString::fromLocal8Bit(argv[5]);
    const QString summary_on = QString::fromLocal8Bit(argv[6]);
    const QString summary_off = QString::fromLocal8Bit(argv[7]);
    const QString order_fixture = QString::fromLocal8Bit(argv[8]);
    const QString outline_fixture = QString::fromLocal8Bit(argv[9]);
    const QString unmodelled_path = QString::fromLocal8Bit(argv[10]);
    const QString corpus_path = argc > 11 ? QString::fromLocal8Bit(argv[11]) : QString();
    // W26-1.2..1.5 add the functional checks that open these fixtures; until
    // then the arguments are accepted (the CMake test entry is final) but not
    // used, and the compiler must not warn.
    (void)order_fixture;

    // The unmodelled backlog is empty (brief: it shrinks to empty or to items
    // deferred under rule 4).
    {
        QFile unmodelled(unmodelled_path);
        gate::check(unmodelled.open(QIODevice::ReadOnly | QIODevice::Text),
                    "the unmodelled backlog file exists");
        int rows = 0;
        while (!unmodelled.atEnd()) {
            const std::string line = unmodelled.readLine().toStdString();
            if (!line.empty() && line[0] != '#' && line != "\n" && line != "\r\n") {
                ++rows;
            }
        }
        gate::check(rows == 0, "ribbon_pin_unmodelled.tsv is empty");
        std::fprintf(stderr, "note: unmodelled backlog rows: %d\n", rows);
    }

    // Prove the classifier on REAL widgets of each public class.
    {
        QWidget parent;
        QToolButton plain_button(&parent);
        QToolButton menu_button(&parent);
        QMenu menu(&menu_button);
        menu_button.setMenu(&menu);
        QComboBox combo(&parent);
        QCheckBox checkbox(&parent);
        QDateEdit date_edit(&parent);
        gate::check(msqt::RibbonBar::control_kind(&plain_button) ==
                        QStringLiteral("button"),
                    "a QToolButton with no menu is a button");
        gate::check(msqt::RibbonBar::control_kind(&menu_button) ==
                        QStringLiteral("button"),
                    "a plain QToolButton with a menu is still a button (split/gallery are subclasses)");
        gate::check(msqt::RibbonBar::control_kind(&combo) == QStringLiteral("combo"),
                    "a QComboBox is a combo, not a button");
        gate::check(msqt::RibbonBar::control_kind(&checkbox) ==
                        QStringLiteral("checkbox"),
                    "a QCheckBox is a checkbox, not a button");
        gate::check(msqt::RibbonBar::control_kind(&date_edit) == QStringLiteral("field"),
                    "a QDateEdit is a field");
        gate::check(msqt::RibbonBar::control_kind(nullptr) == QStringLiteral("none"),
                    "no widget has no kind");
    }

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

    const std::vector<TabPin> pins = load_pin(pin_path);
    gate::check(static_cast<int>(pins.size()) == 7, "the pin has seven command tabs");
    if (pins.size() != 7) {
        std::printf("qt_ribbon_inventory: ");
        return gate::finish();
    }
    gate::check(ribbon->tab_count() == 7, "the ribbon keeps its seven command tabs");
    gate::check(ribbon->tab_titles() ==
                    [&pins] {
                        QStringList titles;
                        for (const TabPin& pin : pins) {
                            titles.push_back(QString::fromUtf8(pin.title.c_str()));
                        }
                        return titles;
                    }(),
                "tab_titles() is exactly the seven measured captions, in order");

    // Load the measured rectangles and palette.
    QFile measures_file(measures_path);
    gate::check(measures_file.open(QIODevice::ReadOnly),
                "the wave-26 measured ribbon file exists");
    const QJsonDocument measures_document =
        QJsonDocument::fromJson(measures_file.readAll());
    const QJsonObject measures = measures_document.object();
    const int top_offset = measures.value(QStringLiteral("controls_top_offset")).toInt(5);
    const int row_height = measures.value(QStringLiteral("stack_row_height")).toInt(22);

    std::vector<int> palette;
    {
        const QJsonArray colours = measures.value(QStringLiteral("palette")).toArray();
        for (const QJsonValue& value : colours) {
            const QJsonArray rgb = value.toArray();
            palette.push_back((rgb.at(0).toInt() << 16) | (rgb.at(1).toInt() << 8) |
                              rgb.at(2).toInt());
        }
    }
    gate::check(palette.size() == 16, "the measured Gantt Chart Style palette has 16 colours");


    // --- every tab's inventory, measured geometry and 1920 layout ----------
    int overflow_tabs_1920 = 0;
    int live_small = 0;
    int live_split = 0;
    int live_gallery = 0;
    int live_launcher = 0;
    int live_checkbox = 0;
    int live_combo = 0;
    int live_field = 0;
    for (std::size_t tab_index = 0; tab_index < pins.size(); ++tab_index) {
        const TabPin& pin = pins[tab_index];
        window.resize(msqt::kParityWidth, msqt::kParityHeight);
        ribbon->set_active_tab(static_cast<int>(tab_index));
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(ribbon->active_tab_index() == static_cast<int>(tab_index),
                    "set_active_tab selects the tab (and checks its tab button)");
        check_inventory(ribbon, pin, measures, static_cast<int>(tab_index), top_offset,
                        row_height);
        check_action_wiring(ribbon, pin);
        for (int group = 0; group < ribbon->group_titles().size(); ++group) {
            for (int control = 0; control < ribbon->group_control_count(group); ++control) {
                const QString kind = ribbon->group_control_kind(group, control);
                live_small += kind == QStringLiteral("small") ? 1 : 0;
                live_split += kind == QStringLiteral("split") ? 1 : 0;
                live_gallery += kind == QStringLiteral("gallery") ? 1 : 0;
                live_launcher += kind == QStringLiteral("launcher") ? 1 : 0;
                live_checkbox += kind == QStringLiteral("checkbox") ? 1 : 0;
                live_combo += kind == QStringLiteral("combo") ? 1 : 0;
                live_field += kind == QStringLiteral("field") ? 1 : 0;
            }
        }
        bool overflowed = false;
        check_layout(ribbon, pin.title.c_str(), &overflowed);
        if (overflowed) {
            ++overflow_tabs_1920;
        }
    }
    std::fprintf(stderr,
                 "note: live control kinds -- small %d, split %d, gallery %d, launcher %d, "
                 "checkbox %d, combo %d, field %d\n",
                 live_small, live_split, live_gallery, live_launcher, live_checkbox,
                 live_combo, live_field);
    gate::check(live_small >= 20, "the small-button stacks are live");
    gate::check(live_split >= 4, "the split buttons are live");
    gate::check(live_gallery == 1, "the Gantt Chart Style gallery is live");
    gate::check(live_launcher >= 2, "the group dialog launchers are live");
    gate::check(live_checkbox >= 8, "the check boxes are live");
    gate::check(live_combo >= 7, "the combos are live");
    gate::check(live_field >= 1, "the Status Date field is live");
    gate::check(overflow_tabs_1920 == 0,
                "at 1920 every tab shows its whole measured inventory");

    // --- the same layout contract at 900x560 -------------------------------
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QApplication::processEvents();
    QApplication::processEvents();
    int overflow_tabs_900 = 0;
    for (std::size_t tab_index = 0; tab_index < pins.size(); ++tab_index) {
        ribbon->set_active_tab(static_cast<int>(tab_index));
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(ribbon->tab_titles().size() == 7,
                    "narrowing the window never changes the tab model");
        bool overflowed = false;
        check_layout(ribbon, pins[tab_index].title.c_str(), &overflowed);
        if (overflowed) {
            ++overflow_tabs_900;
        }
    }
    gate::check(overflow_tabs_900 >= 1, "at 900 at least one tab shows the >> chevron");

    // --- the contextual tab exists only in the Gantt view -------------------
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
            gate::check(ribbon->contextual_tab() != nullptr &&
                            ribbon->contextual_tab()->isVisible(),
                        "the Gantt view shows the contextual tab again");
        }
    }

    // --- the gallery's measured palette -------------------------------------
    {
        ribbon->set_active_tab(6);
        QApplication::processEvents();
        QWidget* gallery = find_by_accessibility(ribbon, QStringLiteral("Gantt Chart Format"),
                                                 QStringLiteral("Gantt Chart Style"));
        gate::check(gallery != nullptr, "the Gantt Chart Style gallery is in the page");
        if (gallery != nullptr) {
            const QList<QToolButton*> swatches =
                gallery->findChildren<QToolButton*>(QStringLiteral("ribbonSwatch"));
            gate::check(swatches.size() == 16, "the gallery has 16 swatches");
            for (int index = 0; index < swatches.size() && index < 16; ++index) {
                const QPixmap pixmap = swatches[index]->icon().pixmap(52, 30);
                const QColor colour = pixmap.toImage().pixelColor(30, 13);
                const int rgb = (colour.red() << 16) | (colour.green() << 8) | colour.blue();
                gate::check(rgb == palette[static_cast<std::size_t>(index)],
                            "a gallery swatch carries the measured palette colour");
                if (rgb != palette[static_cast<std::size_t>(index)]) {
                    std::fprintf(stderr, "  swatch %d live %06x measured %06x\n", index, rgb,
                                 palette[static_cast<std::size_t>(index)]);
                }
            }
            gate::check(gallery->findChildren<QToolButton*>(
                            QStringLiteral("ribbonGalleryExpander"))
                            .size() == 1,
                        "the gallery has its expander");
        }
    }


    // Timeline: the file's stored setting is the initial state and a click
    // toggles the strip only.
    {
        QString open_timeline_error;
        gate::check(window.open_file(timeline_hidden, &open_timeline_error),
                    "the timeline-hidden fixture opens");
        QApplication::processEvents();
        QWidget* timeline_check = find_control(ribbon, QStringLiteral("View"),
                                               QStringLiteral("Split View"),
                                               QStringLiteral("Timeline"));
        gate::check(timeline_check != nullptr, "the Timeline check box is in the page");
        if (timeline_check != nullptr) {
            auto* box = qobject_cast<QCheckBox*>(timeline_check);
            gate::check(box != nullptr, "the Timeline control is a QCheckBox");
            gate::check(!window.timeline_visible(), "the hidden file hides the strip");
            gate::check(box != nullptr && !box->isChecked(),
                        "the Timeline box starts unchecked for the hidden file");
            gate::check(window.timeline() != nullptr && !window.timeline()->isVisible(),
                        "the live strip is hidden");
            if (box != nullptr) {
                box->click();
                QApplication::processEvents();
                gate::check(box->isChecked(), "clicking Timeline checks the box");
                gate::check(window.timeline_visible(), "clicking Timeline shows the strip");
                gate::check(window.timeline()->isVisible(), "the live strip is shown");
                box->click();
                QApplication::processEvents();
                gate::check(!box->isChecked(), "clicking Timeline again unchecks the box");
                gate::check(!window.timeline_visible(), "clicking again hides the strip");
                gate::check(!window.timeline()->isVisible(), "the live strip is hidden again");
            }
        }

        QString open_timeline_shown_error;
        gate::check(window.open_file(timeline_shown, &open_timeline_shown_error),
                    "the timeline-shown fixture opens");
        QApplication::processEvents();
        timeline_check = find_control(ribbon, QStringLiteral("View"),
                                      QStringLiteral("Split View"),
                                      QStringLiteral("Timeline"));
        gate::check(timeline_check != nullptr &&
                        qobject_cast<QCheckBox*>(timeline_check) != nullptr &&
                        qobject_cast<QCheckBox*>(timeline_check)->isChecked(),
                    "the shown file starts with the Timeline box checked");
        gate::check(window.timeline_visible(), "the shown file shows the strip");
    }

    // Project Summary Task: the file state is the initial state; a click adds or
    // removes row 0 and nothing else.
    {
        QString open_summary_error;
        gate::check(window.open_file(summary_on, &open_summary_error),
                    "the summary-on fixture opens");
        QApplication::processEvents();
        QWidget* summary_check = find_control(ribbon, QStringLiteral("Gantt Chart Format"),
                                              QStringLiteral("Show/Hide"),
                                              QStringLiteral("Project Summary Task"));
        gate::check(summary_check != nullptr,
                    "the Project Summary Task check box is in the page");
        const std::size_t rows_with_project = window.sheet_row_count();
        gate::check(window.project_summary_visible(),
                    "the summary-on file starts with the project row shown");
        if (summary_check != nullptr) {
            auto* box = qobject_cast<QCheckBox*>(summary_check);
            gate::check(box != nullptr && box->isChecked(),
                        "the Project Summary Task box starts checked for the file");
            if (box != nullptr) {
                box->click();
                QApplication::processEvents();
                gate::check(!window.project_summary_visible(),
                            "clicking hides the project row");
                gate::check(window.sheet_row_count() + 1 == rows_with_project,
                            "the project row is the one row removed");
                gate::check(!box->isChecked(), "the box follows the off state");
                box->click();
                QApplication::processEvents();
                gate::check(window.project_summary_visible(),
                            "clicking again shows the project row");
                gate::check(window.sheet_row_count() == rows_with_project,
                            "the project row is restored");
            }
        }

        QString open_summary_off_error;
        gate::check(window.open_file(summary_off, &open_summary_off_error),
                    "the summary-off fixture opens");
        QApplication::processEvents();
        summary_check = find_control(ribbon, QStringLiteral("Gantt Chart Format"),
                                     QStringLiteral("Show/Hide"),
                                     QStringLiteral("Project Summary Task"));
        gate::check(summary_check != nullptr &&
                        qobject_cast<QCheckBox*>(summary_check) != nullptr &&
                        !qobject_cast<QCheckBox*>(summary_check)->isChecked(),
                    "the summary-off file starts unchecked");
        gate::check(!window.project_summary_visible(),
                    "the summary-off file starts with the project row hidden");
        if (summary_check != nullptr) {
            const std::size_t rows_without_project = window.sheet_row_count();
            auto* off_box = qobject_cast<QCheckBox*>(summary_check);
            if (off_box != nullptr) {
                off_box->click();
                QApplication::processEvents();
            }
            gate::check(window.sheet_row_count() == rows_without_project + 1,
                        "clicking shows row 0 on a file that stores it hidden");
        }
    }

    // Critical Tasks drives the Gantt highlight.
    {
        QString open_critical_error;
        gate::check(window.open_file(summary_on, &open_critical_error),
                    "the fixture reopens for the Critical Tasks check");
        QApplication::processEvents();
        QWidget* critical = find_control(ribbon, QStringLiteral("Gantt Chart Format"),
                                         QStringLiteral("Bar Styles"),
                                         QStringLiteral("Critical Tasks"));
        gate::check(critical != nullptr, "the Critical Tasks check box is in the page");
        gate::check(!window.critical_tasks_highlight(),
                    "Critical Tasks highlighting starts OFF (the measured default)");
        if (critical != nullptr) {
            auto* box = qobject_cast<QCheckBox*>(critical);
            gate::check(box != nullptr && !box->isChecked(), "the box starts unchecked");
            if (box != nullptr) {
                box->click();
                QApplication::processEvents();
                gate::check(window.critical_tasks_highlight(),
                            "clicking Critical Tasks turns the highlight on");
                box->click();
                QApplication::processEvents();
                gate::check(!window.critical_tasks_highlight(),
                            "clicking again turns the highlight off");
            }
        }
    }

    // %-complete buttons: the ribbon queues app::StructuralKind::set_percent
    // (app/edit_model.h), the path the save cascade writes, and the selected
    // task's plan value changes immediately.
    {
        QString open_percent_error;
        gate::check(window.open_file(fixture, &open_percent_error),
                    "the fixture reopens for the %-complete edit");
        QApplication::processEvents();
        const std::size_t rows = window.sheet_row_count();
        std::uint32_t uid = 0;
        for (std::size_t row = 0; row < window.sheet()->row_count(); ++row) {
            const std::uint32_t candidate = window.sheet()->row_uid(row);
            if (candidate != 0) {
                uid = candidate;
                break;
            }
        }
        gate::check(uid != 0, "the fixture has a task row");
        gate::check(uid == 0 || select_uid(window, uid),
                    "a task row is selected for the %-complete edit");
        // NOTE: find_control() rebuilds the ribbon page, so a control pointer
        // from an earlier call must never be used after another one.
        QWidget* percent = find_control(ribbon, QStringLiteral("Task"),
                                        QStringLiteral("Schedule"), QStringLiteral("25%"));
        gate::check(percent != nullptr, "the 25% button is in the page");
        if (percent != nullptr && uid != 0) {
            click_control(percent);
            QApplication::processEvents();
            const app::TaskRow* task = plan_task(window, uid);
            gate::check(task != nullptr && task->percent_complete == 25,
                        "the 25% button sets the selected task's % Complete");
            if (task != nullptr && task->percent_complete != 25) {
                std::fprintf(stderr, "  task uid %u percent_complete %u\n", uid,
                             static_cast<unsigned>(task->percent_complete));
            }
            QWidget* hundred = find_control(ribbon, QStringLiteral("Task"),
                                            QStringLiteral("Schedule"),
                                            QStringLiteral("100%"));
            gate::check(hundred != nullptr, "the 100% button is in the page");
            if (hundred != nullptr) {
                click_control(hundred);
                QApplication::processEvents();
                const app::TaskRow* done = plan_task(window, uid);
                gate::check(done != nullptr && done->percent_complete == 100,
                            "the 100% button overrides the earlier value (last edit wins)");
            }
        }
        gate::check(window.sheet_row_count() == rows,
                    "the %-complete edit never adds or removes a row");

        // On the corpus file the stored table SHOWS % Complete: the rebuilt
        // RecordSet must make the read-only display cell follow the edit (the
        // edit itself never goes through that column).
        if (!corpus_path.isEmpty()) {
            QString open_corpus_error;
            gate::check(window.open_file(corpus_path, &open_corpus_error),
                        "the corpus file (stored table with % Complete) opens");
            QApplication::processEvents();
            std::uint32_t corpus_uid = 0;
            std::size_t corpus_row = 0;
            for (std::size_t row = 0; row < window.sheet()->row_count(); ++row) {
                const std::uint32_t candidate = window.sheet()->row_uid(row);
                if (candidate != 0) {
                    corpus_uid = candidate;
                    corpus_row = row;
                    break;
                }
            }
            gate::check(corpus_uid != 0 && select_uid(window, corpus_uid),
                        "a corpus task row is selected");
            percent = find_control(ribbon, QStringLiteral("Task"),
                                   QStringLiteral("Schedule"), QStringLiteral("25%"));
            if (percent != nullptr && corpus_uid != 0) {
                click_control(percent);
                QApplication::processEvents();
                const app::TaskRow* task = plan_task(window, corpus_uid);
                gate::check(task != nullptr && task->percent_complete == 25,
                            "the corpus task's plan value is 25 % complete");
                std::size_t percent_column = core::sheet::kNoIndex;
                for (std::size_t candidate = 0; candidate < window.sheet()->column_count();
                     ++candidate) {
                    if (window.sheet()->column(candidate).field ==
                        app::task_field_id("PercentComplete")) {
                        percent_column = candidate;
                        break;
                    }
                }
                gate::check(percent_column != core::sheet::kNoIndex,
                            "the corpus stored table shows % Complete");
                if (percent_column != core::sheet::kNoIndex && task != nullptr) {
                    const std::string text = app::cell_text(*window.sheet(), corpus_row,
                                                            percent_column,
                                                            window.preferences());
                    gate::check(text == "25%",
                                "the displayed % Complete cell follows the edit");
                    if (text != "25%") {
                        std::fprintf(stderr, "  displayed %% Complete: '%s'\n", text.c_str());
                    }
                }
            }
        }
    }

    // Unlink removes the selected task's saved predecessor links through the
    // shared link-edit path; the display plan's link list shrinks.
    {
        QString open_unlink_error;
        gate::check(window.open_file(outline_fixture, &open_unlink_error),
                    "the linked fixture opens for the Unlink check");
        QApplication::processEvents();
        std::uint32_t successor = 0;
        for (const app::LinkRow& link : window.plan().links) {
            if (link.successor_uid != 0) {
                successor = link.successor_uid;
                break;
            }
        }
        gate::check(successor != 0, "the fixture carries a saved link");
        const std::size_t links_before = window.plan().links.size();
        gate::check(successor == 0 || select_uid(window, successor),
                    "the linked successor is selected");
        QWidget* unlink = find_control(ribbon, QStringLiteral("Task"),
                                       QStringLiteral("Schedule"),
                                       QStringLiteral("unlink tasks"));
        gate::check(unlink != nullptr, "the Unlink button is in the page");
        if (unlink != nullptr && successor != 0) {
            click_control(unlink);
            QApplication::processEvents();
            gate::check(window.plan().links.size() < links_before,
                        "Unlink removes the selected task's predecessor links");
            std::fprintf(stderr, "note: links %zu -> %zu after Unlink\n", links_before,
                         window.plan().links.size());
        }
        // The outdent/indent/split-task buttons are disabled with the exact
        // tooltip: the shared sheet model has no outline/split edit
        // (core/sheet/sheet.h: "No row insert/delete/move or indent/outdent").
        for (const char* label : {"outdent", "indent", "split task"}) {
            QWidget* control = find_control(ribbon, QStringLiteral("Task"),
                                            QStringLiteral("Schedule"),
                                            QString::fromUtf8(label));
            gate::check(control != nullptr && !control->isEnabled() &&
                            control->toolTip() ==
                                QStringLiteral("not implemented in this port"),
                        "a schedule icon without an engine edit is disabled with the exact tooltip");
        }
    }

    std::printf("qt_ribbon_inventory: ");
    return gate::finish();
}
