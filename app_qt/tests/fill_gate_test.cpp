// app_qt/tests/fill_gate_test.cpp -- wave 30 (W30-1.8), reworked in wave 31
// (W31-0.4 / R30-4).
//
// WHY FILL DOWN IS DISABLED.  W30-1.8 enabled Fill Down as a .mpp write on the
// strength of an uncommitted corpus probe.  W31-0.4 measured Project 2021's own
// Fill Down of Start over two top-level automatic leaf rows
// (tests/data/wave31-fill-control.mpp -> wave31-fill.mpp, with the control's
// no-edit save): Project's save rewrites the edited row's constraint AND its
// calculated/effective dates and slack words, the project-row roll-up, appends
// a Task Var2 record and rewrites the DocumentSummaryInformation summary
// strings.  The port's start writer (mppwrite::save_date_edits) writes only the
// 6-byte constraint window of the edited row (plus the manual bit), so the pair
// cannot be reproduced through tests/save_pair_noise.h.  Rule 1 therefore keeps
// Fill Down off: the ribbon button, the row menu entry and the pin all say
// disabled, with decision 9's exact tooltip.
//
// WHAT THIS GATE DOES
//   1. finds the Task tab / Editing group's Fill control: it exists, is wired
//      to RibbonCommand::fill_down, is DISABLED and carries the exact
//      decision-9 tooltip; a click stages nothing and does not dirty the file;
//   2. builds the row menu and asserts its Fill Down action is disabled with
//      the same tooltip;
//   3. opens the committed measured pair READ-ONLY through the same MainWindow
//      and pins the facts the pair documents: the control's Task B starts at
//      the project start 2026-01-05 and finishes 2026-01-07 17:00; Project's
//      fill save starts it 2026-01-12 08:00 and finishes it 2026-01-14 17:00;
//      both rows are automatic, 0 % non-summaries on the project calendar.
//      This keeps the pair under CI as the target a future full start writer
//      must reproduce (tests/data/README.md has the byte effects).
//
// The fill_down() handler keeps its shape refusals (W31-0.4's U1: a summary
// row is refused before anything is staged), but the disabled control means
// the UI cannot reach them; this gate asserts the disabled state instead.
//
// Usage: qt_fill_gate <fixture.mpp> [out-dir]
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMenu>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "sheet.h"
#include "shell_style.h"

namespace {

QAbstractButton* fill_button(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int task_tab = ribbon->tab_titles().indexOf(QStringLiteral("Task"));
    if (task_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(task_tab);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int editing = groups.indexOf(QStringLiteral("Editing"));
    if (editing < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(editing); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(editing, control));
        if (button != nullptr && button->text() == QStringLiteral("Fill")) {
            return button;
        }
    }
    return nullptr;
}

const app::TaskRow* task_for_uid(msqt::MainWindow& window, std::uint32_t uid) {
    for (const app::TaskRow& task : window.plan().tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

core::sched::date32 at(int year, int month, int day, int hour, int minute) {
    return core::sched::from_ymd(year, month, day, hour, minute);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_fill_gate <fixture.mpp> [out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    const QDir data_dir = QFileInfo(fixture).dir();

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, 800);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    core::sheet::SheetModel* sheet = window.sheet();
    gate::check(sheet != nullptr, "the fixture has a sheet");
    if (sheet == nullptr) {
        return gate::finish();
    }

    // --- 1. the ribbon control is disabled, wired, and inert ---------------
    QAbstractButton* fill = fill_button(window);
    gate::check(fill != nullptr, "the Task tab's Editing group carries Fill");
    if (fill == nullptr) {
        return gate::finish();
    }
    gate::check(!fill->isEnabled(), "Fill is disabled (W31-0.4, R30-4)");
    gate::check(fill->toolTip() == QStringLiteral("not implemented in this port"),
                "Fill carries the exact decision-9 tooltip");
    {
        msqt::RibbonBar* ribbon = window.ribbon();
        const QStringList groups = ribbon->group_titles();
        const int editing = groups.indexOf(QStringLiteral("Editing"));
        bool found = false;
        for (int control = 0; control < ribbon->group_control_count(editing); ++control) {
            if (ribbon->group_control(editing, control) == fill) {
                found = ribbon->group_control_command(editing, control) ==
                        msqt::RibbonCommand::fill_down;
                break;
            }
        }
        gate::check(found, "the disabled control is wired to RibbonCommand::fill_down");
    }
    // A disabled button must be inert: click() is a no-op, so nothing is staged
    // and the document stays clean.
    fill->click();
    QApplication::processEvents();
    gate::check(window.pending_start_edits() == 0,
                "a click on the disabled Fill stages no start edit");
    gate::check(!window.document_dirty(), "a click on the disabled Fill does not dirty the file");
    gate::check(!sheet->can_undo(), "a click on the disabled Fill records no undo step");

    // --- 2. the row menu entry is disabled with the same tooltip -----------
    {
        QMenu* menu = window.build_row_menu();
        QAction* row_fill = nullptr;
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Fill Down")) {
                row_fill = action;
            }
        }
        gate::check(row_fill != nullptr, "the row menu carries Fill Down");
        gate::check(row_fill != nullptr && !row_fill->isEnabled(),
                    "the row menu's Fill Down is disabled (W31-0.4, R30-4)");
        gate::check(row_fill != nullptr &&
                        row_fill->toolTip() == QStringLiteral("not implemented in this port"),
                    "the row menu's Fill Down carries the exact decision-9 tooltip");
        delete menu;
    }

    // --- 3. the committed measured pair's facts ---------------------------
    {
        const QString control_path = data_dir.filePath(QStringLiteral("wave31-fill-control.mpp"));
        const QString edit_path = data_dir.filePath(QStringLiteral("wave31-fill.mpp"));
        gate::check(QFileInfo::exists(control_path), "the measured Fill Down control is committed");
        gate::check(QFileInfo::exists(edit_path), "the measured Fill Down edit is committed");

        msqt::MainWindow control_window;
        QString control_error;
        gate::check(control_window.open_file(control_path, &control_error),
                    "the Fill Down control loads read-only");
        const app::TaskRow* control_a = task_for_uid(control_window, 1);
        const app::TaskRow* control_b = task_for_uid(control_window, 2);
        gate::check(control_a != nullptr && control_b != nullptr,
                    "the control carries the two measured leaf rows");
        if (control_a != nullptr && control_b != nullptr) {
            gate::check(control_a->start == at(2026, 1, 12, 8, 0),
                        "the control's Task A starts 2026-01-12 08:00");
            gate::check(control_b->start == at(2026, 1, 5, 8, 0),
                        "the control's Task B starts at the project start");
            gate::check(control_b->finish == at(2026, 1, 7, 17, 0),
                        "the control's Task B finishes 2026-01-07 17:00");
            gate::check(control_b->percent_complete == 0 && !control_b->summary &&
                            !control_b->manually_scheduled &&
                            control_b->calendar_uid == -1,
                        "the control's Task B is a 0 % automatic leaf on the project calendar");
        }

        msqt::MainWindow edit_window;
        QString edit_error;
        gate::check(edit_window.open_file(edit_path, &edit_error),
                    "Project's Fill Down edit loads read-only");
        const app::TaskRow* edit_a = task_for_uid(edit_window, 1);
        const app::TaskRow* edit_b = task_for_uid(edit_window, 2);
        gate::check(edit_a != nullptr && edit_b != nullptr,
                    "the edit carries the two measured leaf rows");
        if (edit_a != nullptr && edit_b != nullptr) {
            gate::check(edit_a->start == at(2026, 1, 12, 8, 0),
                        "Project's fill leaves the source row's start alone");
            gate::check(edit_b->start == at(2026, 1, 12, 8, 0),
                        "Project's fill writes Task B's start down from Task A");
            gate::check(edit_b->finish == at(2026, 1, 14, 17, 0),
                        "Project's fill recalculates Task B's finish");
            gate::check(edit_b->percent_complete == 0 && !edit_b->summary &&
                            !edit_b->manually_scheduled &&
                            edit_b->calendar_uid == -1,
                        "Project's fill keeps Task B an automatic 0 % leaf");
        }
    }

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(window.grab().save(out_dir + QStringLiteral("/fill-disabled.png"), "PNG"),
                    "the disabled control is captured for human review");
    }

    return gate::finish();
}
