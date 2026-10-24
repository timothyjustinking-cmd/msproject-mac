// qt_outline_display -- wave 28 (W28-2.2): the sheet and Gantt show the new
// outline the moment an Indent or Outdent is queued.
//
// WHAT THIS PROVES
//   * After Edit > Indent Task on uid 2 (the measured leaf-under-sibling case),
//     with no save and no reopen:
//       - the sheet moves uid 2 to level 1 and indents it by exactly one
//         SheetMetrics::indent_per_level;
//       - its OutlineNumber becomes 1.1 and uid 3 renumbers 3 -> 2;
//       - uid 1 becomes a summary with a working collapse triangle:
//         toggle_collapsed() hides uid 2's row and expanding it brings the row
//         back;
//       - the Gantt's uid 1 bar becomes a summary bar (kind summary, shape
//         summary_bracket); uid 2 stays a normal bar.
//   * Undo restores the flat display and redo reapplies it -- the same
//     PlanDocument rebuild the menu drives.
//   * The outdent control (uid 2 already the only child of summary uid 1)
//     proves the reverse: level 1 -> 0, the indent goes back to 0, uid 1 stops
//     being a summary, and its Gantt bar returns to normal.
//
// The two committed controls are the files Project saved in W28-2.1; the
// expected levels/numbers are the reader's view of Project's own reopen.  Both
// are opened read-only through MainWindow: nothing writes tests/data.
//
// Usage: qt_outline_display <indent-control.mpp> <outdent-control.mpp>

#include <QAction>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "gantt.h"
#include "gantt_canvas.h"
#include "gate_util.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "selection_model.h"
#include "sheet.h"
#include "shell_style.h"

namespace {

// The SheetModel row showing `uid`, or row_count() when absent.
std::size_t row_of(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return 0;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        if (sheet->row_uid(row) == uid) {
            return row;
        }
    }
    return sheet->row_count();
}

// The displayed OutlineNumber cell for `uid`.
std::string outline_number(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return std::string();
    }
    const std::size_t row = row_of(window, uid);
    if (row >= sheet->row_count()) {
        return std::string();
    }
    return sheet->field_text(row, app::task_field_id("OutlineNumber"));
}

const core::gantt::Bar* bar_for(msqt::MainWindow& window, std::uint32_t uid) {
    if (window.gantt_view() == nullptr) {
        return nullptr;
    }
    return window.gantt_view()->layout().bar(uid);
}

bool select_uid(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return false;
    }
    const std::size_t row = row_of(window, uid);
    if (row >= sheet->row_count()) {
        return false;
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

void trigger(msqt::MainWindow& window, app::MenuAction id, const char* what) {
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, what);
    if (action == nullptr) {
        return;
    }
    gate::check(action->isEnabled(), what);
    if (action->isEnabled()) {
        action->trigger();
        QApplication::processEvents();
    }
}

// Common snapshot: level and indent of uid 2, summary facts of uid 1, the
// displayed OutlineNumbers, and the two Gantt bar shapes.
struct Display {
    int level = -1;
    int indent = -1;
    bool anchor_summary = false;
    bool anchor_has_children = false;
    std::string number_1;
    std::string number_2;
    std::string number_3;
    core::gantt::BarKind kind_1 = core::gantt::BarKind::normal;
    core::gantt::BarShape shape_1 = core::gantt::BarShape::rectangle;
    core::gantt::BarKind kind_2 = core::gantt::BarKind::normal;
    core::gantt::BarShape shape_2 = core::gantt::BarShape::rectangle;
    std::size_t visible_rows = 0;
};

Display snapshot(msqt::MainWindow& window) {
    Display display;
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return display;
    }
    const core::sheet::SheetMetrics metrics;
    const std::size_t row_1 = row_of(window, 1);
    const std::size_t row_2 = row_of(window, 2);
    if (row_2 < sheet->row_count()) {
        display.level = sheet->outline_level(row_2);
        display.indent = sheet->indent_points(row_2, metrics);
    }
    if (row_1 < sheet->row_count()) {
        display.anchor_summary = sheet->is_summary(row_1);
        display.anchor_has_children = sheet->has_children(row_1);
    }
    display.number_1 = outline_number(window, 1);
    display.number_2 = outline_number(window, 2);
    display.number_3 = outline_number(window, 3);
    if (const core::gantt::Bar* bar = bar_for(window, 1)) {
        display.kind_1 = bar->kind;
        display.shape_1 = bar->shape;
    }
    if (const core::gantt::Bar* bar = bar_for(window, 2)) {
        display.kind_2 = bar->kind;
        display.shape_2 = bar->shape;
    }
    display.visible_rows = sheet->visible_row_count();
    return display;
}

void check_flat(const Display& display, const char* when) {
    const std::string prefix = std::string("after ") + when + ": ";
    gate::check(display.level == 0, (prefix + "uid 2 is at level 0").c_str());
    gate::check(display.indent == 0, (prefix + "uid 2 has no indent").c_str());
    gate::check(!display.anchor_summary, (prefix + "uid 1 is not a summary").c_str());
    gate::check(!display.anchor_has_children,
                (prefix + "uid 1 has no children").c_str());
    gate::check(display.number_1 == "1", (prefix + "uid 1 shows outline 1").c_str());
    gate::check(display.number_2 == "2", (prefix + "uid 2 shows outline 2").c_str());
    gate::check(display.number_3 == "3", (prefix + "uid 3 shows outline 3").c_str());
    gate::check(display.kind_1 == core::gantt::BarKind::normal,
                (prefix + "uid 1's Gantt bar is normal").c_str());
    gate::check(display.shape_1 == core::gantt::BarShape::rectangle,
                (prefix + "uid 1's Gantt bar is a rectangle").c_str());
    gate::check(display.kind_2 == core::gantt::BarKind::normal,
                (prefix + "uid 2's Gantt bar is normal").c_str());
}

void check_indented(const Display& display, const char* when) {
    const std::string prefix = std::string("after ") + when + ": ";
    gate::check(display.level == 1, (prefix + "uid 2 is at level 1").c_str());
    gate::check(display.indent == core::sheet::SheetMetrics{}.indent_per_level,
                (prefix + "uid 2 is indented one level").c_str());
    gate::check(display.anchor_summary, (prefix + "uid 1 is a summary").c_str());
    gate::check(display.anchor_has_children,
                (prefix + "uid 1 has a collapse triangle").c_str());
    gate::check(display.number_1 == "1", (prefix + "uid 1 shows outline 1").c_str());
    gate::check(display.number_2 == "1.1", (prefix + "uid 2 shows outline 1.1").c_str());
    gate::check(display.number_3 == "2", (prefix + "uid 3 renumbers to outline 2").c_str());
    gate::check(display.kind_1 == core::gantt::BarKind::summary,
                (prefix + "uid 1's Gantt bar is a summary").c_str());
    gate::check(display.shape_1 == core::gantt::BarShape::summary_bracket,
                (prefix + "uid 1's Gantt bar is a summary bracket").c_str());
    gate::check(display.kind_2 == core::gantt::BarKind::normal,
                (prefix + "uid 2's Gantt bar stays normal").c_str());
}

void case_indent(const QString& path) {
    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(path, &error), "the indent control opens");
    if (!error.isEmpty()) {
        std::fprintf(stderr, "note: open error: %s\n", error.toUtf8().constData());
        return;
    }
    window.show();
    QApplication::processEvents();

    gate::check(window.sheet() != nullptr, "the indent control has a sheet");
    if (window.sheet() == nullptr) {
        return;
    }
    gate::check(window.gantt_view() != nullptr, "the indent control has a Gantt");

    const Display before = snapshot(window);
    check_flat(before, "opening the indent control");

    gate::check(select_uid(window, 2), "uid 2 can be selected");
    trigger(window, app::MenuAction::indent, "Edit > Indent Task is available");

    const Display indented = snapshot(window);
    check_indented(indented, "Indent");
    gate::check(indented.visible_rows == before.visible_rows,
                "an indented summary is expanded: every row stays visible");

    // The collapse triangle works at once: collapsing uid 1 hides its child,
    // expanding it brings the same row back.
    core::sheet::SheetModel* sheet = window.sheet();
    const std::size_t row_1 = row_of(window, 1);
    const std::size_t row_2 = row_of(window, 2);
    gate::check(sheet != nullptr && row_1 < sheet->row_count() &&
                    row_2 < sheet->row_count(),
                "the indented rows are addressable");
    if (sheet != nullptr && row_1 < sheet->row_count() && row_2 < sheet->row_count()) {
        gate::check(sheet->row_visible(row_2), "uid 2's row is visible before collapsing");
        gate::check(sheet->toggle_collapsed(row_1),
                    "the summary's collapse triangle accepts the click");
        gate::check(!sheet->row_visible(row_2),
                    "collapsing uid 1 hides uid 2's row");
        gate::check(sheet->visible_row_count() == indented.visible_rows - 1,
                    "collapsing uid 1 removes exactly one row");
        gate::check(sheet->toggle_collapsed(row_1),
                    "the expanded triangle accepts the second click");
        gate::check(sheet->row_visible(row_2),
                    "expanding uid 1 shows uid 2's row again");
        gate::check(sheet->visible_row_count() == indented.visible_rows,
                    "expanding uid 1 restores the row count");
    }

    trigger(window, app::MenuAction::undo, "Undo restores the sheet at once");
    check_flat(snapshot(window), "Undo");

    trigger(window, app::MenuAction::redo, "Redo reapplies the outline at once");
    check_indented(snapshot(window), "Redo");
}

void case_outdent(const QString& path) {
    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(path, &error), "the outdent control opens");
    if (!error.isEmpty()) {
        std::fprintf(stderr, "note: open error: %s\n", error.toUtf8().constData());
        return;
    }
    window.show();
    QApplication::processEvents();

    gate::check(window.sheet() != nullptr, "the outdent control has a sheet");
    if (window.sheet() == nullptr) {
        return;
    }
    gate::check(window.gantt_view() != nullptr, "the outdent control has a Gantt");

    const Display before = snapshot(window);
    check_indented(before, "opening the outdent control");

    gate::check(select_uid(window, 2), "uid 2 can be selected");
    trigger(window, app::MenuAction::outdent, "Edit > Outdent Task is available");

    const Display outdented = snapshot(window);
    check_flat(outdented, "Outdent");
    gate::check(outdented.visible_rows == before.visible_rows,
                "outdenting keeps every row visible");

    // uid 1 is a leaf again: its triangle must refuse to collapse anything.
    core::sheet::SheetModel* sheet = window.sheet();
    const std::size_t row_1 = row_of(window, 1);
    if (sheet != nullptr && row_1 < sheet->row_count()) {
        gate::check(!sheet->toggle_collapsed(row_1),
                    "a former summary's triangle refuses to collapse");
        gate::check(sheet->visible_row_count() == outdented.visible_rows,
                    "the refused collapse leaves the row count unchanged");
    }

    trigger(window, app::MenuAction::undo, "Undo restores the summary at once");
    check_indented(snapshot(window), "Undo");

    trigger(window, app::MenuAction::redo, "Redo flattens the outline at once");
    check_flat(snapshot(window), "Redo");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_outline_display <indent-control.mpp> "
                     "<outdent-control.mpp>\n");
        return 2;
    }

    case_indent(QString::fromLocal8Bit(argv[1]));
    case_outdent(QString::fromLocal8Bit(argv[2]));

    std::printf("qt_outline_display: %d failure(s)\n", gate::failures);
    return gate::finish();
}
