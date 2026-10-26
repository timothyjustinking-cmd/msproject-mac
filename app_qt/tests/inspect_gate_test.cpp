// app_qt/tests/inspect_gate_test.cpp -- wave 30 (W30-1.10).
//
// THE TARGET (wave 30 brief, G1): Inspect is a READ-ONLY pane made from the
// scheduler's facts.  This port has no Task Inspector; this gate proves the
// pane the Task tab's Inspect button now opens:
//   * it is built from app::PlanInput by UID (never a RecordSet row index),
//     so its dates/duration/percent/link texts are the same facts the Gantt
//     and the computed sheet columns are built from;
//   * every field is read-only and the pane has no write path -- opening it,
//     following the selection and closing it never dirty the document;
//   * the ribbon button is enabled, carries RibbonCommand::inspect and
//     toggles the pane (there is no View menu check box for it);
//   * the pane follows the sheet selection while it stays open, presents the
//     synthetic project summary row from the file's project facts, and says a
//     pending Insert Task row is "(not saved yet)" rather than inventing a
//     stored mode;
//   * switching to another view hides it and returning re-docks it (the
//     preference survives, exactly like the Task Form).
//
// THE FIXTURE: tests/data/wave23-outline.mpp -- six synthetic tasks, a 3-level
// outline (uid 1/2 summaries, outline 1 / 1.1 / 1.1.1 / 1.1.2 / 1.2 / 2), three
// links (3 -> 4 FS+2 days, 3 -> 5 SS+1 day, 4 -> 6 FF) and one named
// assignment (uid 6 -> "Resource A", uid 3 unassigned).  Those texts are the
// COM-measured values task_order_test already pins, so this gate asserts them
// independently of the pane.
//
// Usage: qt_inspect_gate <fixture.mpp> [out-dir]
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
// Qt 6.5.3's qtestmouse.h trips MSVC /W4 C4701 (an uninitialised local in the
// inline mouse helper).  The gate builds with /WX, so scope the warning off for
// this third-party header only.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4701)
#endif
#include <QTest>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

#include "dialogs.h"       // TaskInformationValues (the mode cross-check)
#include "entry_columns.h"  // entry_field(), kPredecessorsField, kResourceNamesField
#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "sheet.h"
#include "shell_style.h"
#include "task_inspector.h"

namespace {

QString text_of(const QLineEdit* field) {
    return field != nullptr ? field->text() : QString();
}

QAbstractButton* inspect_button(msqt::MainWindow& window) {
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
    const int tasks = groups.indexOf(QStringLiteral("Tasks"));
    if (tasks < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(tasks); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(tasks, control));
        if (button != nullptr && button->text() == QStringLiteral("Inspect")) {
            return button;
        }
    }
    return nullptr;
}

// Clicks the LIVE Inspect button.  RibbonBar::set_active_tab() (inside
// inspect_button) rebuilds the page and deletes its controls, so a pointer
// cached across a tab switch is stale: find it again on every click.
bool click_inspect(msqt::MainWindow& window) {
    QAbstractButton* button = inspect_button(window);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    return true;
}

int name_column(QAbstractItemModel* model) {
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() ==
            QStringLiteral("Task Name")) {
            return column;
        }
    }
    return -1;
}

int display_row_for_uid(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    QAbstractItemModel* model = window.sheet_view()->model();
    if (sheet == nullptr || model == nullptr) {
        return -1;
    }
    for (int row = 0; row < model->rowCount(); ++row) {
        const std::size_t record = sheet->visible_row(static_cast<std::size_t>(row));
        if (record != core::sheet::kNoIndex && sheet->row_uid(record) == uid) {
            return row;
        }
    }
    return -1;
}

// Select `uid` with a real mouse click on its Task Name cell, so the click
// handler (and the pane refresh it performs) runs exactly as it does live.
bool click_task(msqt::MainWindow& window, std::uint32_t uid) {
    QAbstractItemModel* model = window.sheet_view()->model();
    const int row = display_row_for_uid(window, uid);
    const int column = name_column(model);
    if (row < 0) {
        return false;
    }
    const QModelIndex index = model->index(row, column < 0 ? 0 : column);
    window.sheet_view()->scrollTo(index);
    QApplication::processEvents();
    const QRect rect = window.sheet_view()->visualRect(index);
    QTest::mousePress(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      rect.center());
    QTest::mouseRelease(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                        rect.center());
    QApplication::processEvents();
    return true;
}

std::size_t row_for_uid(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return core::sheet::kNoIndex;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        if (sheet->row_uid(row) == uid) {
            return row;
        }
    }
    return core::sheet::kNoIndex;
}

// The sheet cell text for a task's field (the pane must agree with it for the
// facts the sheet also shows: Name, Start, Finish, Duration, Predecessors).
std::string sheet_text(msqt::MainWindow& window, std::uint32_t uid,
                       core::view::FieldId field) {
    core::sheet::SheetModel* sheet = window.sheet();
    auto* model = window.sheet_table_model();
    const std::size_t row = row_for_uid(window, uid);
    if (sheet == nullptr || model == nullptr || row == core::sheet::kNoIndex) {
        return std::string();
    }
    const std::size_t column = model->visible_column_for_field(field);
    if (column == core::sheet::kNoIndex) {
        return std::string();
    }
    return sheet->cell_text(row, column);
}

void trigger(msqt::MainWindow& window, app::MenuAction id) {
    window.refresh_menu_state();
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, "the menu carries the action");
    if (action != nullptr) {
        action->trigger();
        QApplication::processEvents();
    }
}

// Toggles the Project Summary Task check box on the Gantt Chart Format tab and
// returns the live setting.  A view command, never a file write.
bool show_project_summary(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return false;
    }
    const int tab = ribbon->tab_titles().indexOf(QStringLiteral("Gantt Chart Format"));
    if (tab < 0) {
        return false;
    }
    ribbon->set_active_tab(tab);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int show_hide = groups.indexOf(QStringLiteral("Show/Hide"));
    if (show_hide < 0) {
        return false;
    }
    for (int control = 0; control < ribbon->group_control_count(show_hide); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(show_hide, control));
        if (button == nullptr || button->text() != QStringLiteral("Project Summary Task")) {
            continue;
        }
        if (!button->isChecked()) {
            button->click();
            QApplication::processEvents();
            QApplication::processEvents();
        }
        return window.project_summary_visible();
    }
    return false;
}

struct InputDialogResult {
    bool seen = false;
};

void accept_input_dialog(InputDialogResult* seen, const QString& text) {
    QTimer::singleShot(0, [seen, text]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QInputDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible()) {
                continue;
            }
            seen->seen = true;
            dialog->setTextValue(text);
            dialog->accept();
            return;
        }
    });
}

std::set<std::uint32_t> uids_in_sheet(msqt::MainWindow& window) {
    std::set<std::uint32_t> uids;
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return uids;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        uids.insert(sheet->row_uid(row));
    }
    return uids;
}

// The pane's fields are all read-only: a QLineEdit the user could type into
// would be a write path with no model behind it.
bool all_fields_read_only(msqt::TaskInspector* inspector) {
    if (inspector == nullptr) {
        return false;
    }
    const QLineEdit* fields[] = {
        inspector->name_field(),        inspector->id_field(),
        inspector->uid_field(),         inspector->outline_field(),
        inspector->type_field(),        inspector->mode_field(),
        inspector->start_field(),       inspector->finish_field(),
        inspector->duration_field(),    inspector->percent_field(),
        inspector->critical_field(),    inspector->calendar_field(),
        inspector->predecessors_field(), inspector->successors_field(),
        inspector->resources_field(),
    };
    for (const QLineEdit* field : fields) {
        if (field == nullptr || !field->isReadOnly()) {
            return false;
        }
    }
    return true;
}

bool fields_empty(msqt::TaskInspector* inspector) {
    if (inspector == nullptr) {
        return false;
    }
    return text_of(inspector->name_field()).isEmpty() &&
           text_of(inspector->id_field()).isEmpty() &&
           text_of(inspector->uid_field()).isEmpty() &&
           text_of(inspector->outline_field()).isEmpty() &&
           text_of(inspector->type_field()).isEmpty() &&
           text_of(inspector->mode_field()).isEmpty() &&
           text_of(inspector->start_field()).isEmpty() &&
           text_of(inspector->finish_field()).isEmpty() &&
           text_of(inspector->duration_field()).isEmpty() &&
           text_of(inspector->percent_field()).isEmpty() &&
           text_of(inspector->critical_field()).isEmpty() &&
           text_of(inspector->calendar_field()).isEmpty() &&
           text_of(inspector->predecessors_field()).isEmpty() &&
           text_of(inspector->successors_field()).isEmpty() &&
           text_of(inspector->resources_field()).isEmpty();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_inspect_gate <fixture.mpp> [out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, 800);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    core::sheet::SheetModel* sheet = window.sheet();
    gate::check(sheet != nullptr, "the fixture has a sheet");
    msqt::TaskInspector* inspector = window.task_inspector();
    gate::check(inspector != nullptr, "the window carries the Task Inspector pane");
    if (sheet == nullptr || inspector == nullptr) {
        return gate::finish();
    }

    // --- 1. the live ribbon button and its wiring --------------------------
    QAbstractButton* inspect = inspect_button(window);
    gate::check(inspect != nullptr, "the Task tab's Tasks group carries Inspect");
    gate::check(inspect != nullptr && inspect->isEnabled(), "Inspect is live (W30-1.10)");
    gate::check(inspect != nullptr && inspect->toolTip() == QStringLiteral("Inspect"),
                "the enabled Inspect button carries its live tooltip");
    if (inspect == nullptr) {
        return gate::finish();
    }
    {
        msqt::RibbonBar* ribbon = window.ribbon();
        const QStringList groups = ribbon->group_titles();
        const int tasks = groups.indexOf(QStringLiteral("Tasks"));
        bool wired = false;
        for (int control = 0; control < ribbon->group_control_count(tasks); ++control) {
            if (ribbon->group_control(tasks, control) == inspect) {
                wired = ribbon->group_control_command(tasks, control) ==
                        msqt::RibbonCommand::inspect;
                break;
            }
        }
        gate::check(wired, "the live button is wired to RibbonCommand::inspect");
    }
    gate::check(!window.inspector_visible(), "the Task Inspector starts hidden");
    gate::check(!inspector->isVisible(), "the hidden pane is not on screen");
    gate::check(all_fields_read_only(inspector),
                "every inspector field is read-only (no write path)");

    // --- 2. no selection: the pane opens empty and says so -----------------
    sheet->clear_selection();
    gate::check(click_inspect(window), "the Inspect button is still live");
    gate::check(window.inspector_visible() && inspector->isVisible(),
                "Inspect shows the Task Inspector");
    gate::check(inspector->selection_label()->text() == QStringLiteral("No task selected"),
                "the empty pane says no task is selected");
    gate::check(fields_empty(inspector), "the empty pane clears every field");
    gate::check(!window.document_dirty(), "showing the pane does not dirty the document");

    // --- 3. a leaf: the scheduler facts come from the plan by UID ----------
    gate::check(click_task(window, 4), "uid 4 has a sheet row");
    gate::check(text_of(inspector->id_field()) == QStringLiteral("4"),
                "the pane follows the click to uid 4's ID");
    gate::check(text_of(inspector->uid_field()) == QStringLiteral("4"),
                "the pane shows uid 4's Unique ID");
    gate::check(text_of(inspector->name_field()) ==
                    QString::fromStdString(
                        sheet_text(window, 4, msqt::entry_field(msqt::EntryCellKind::name))),
                "the pane's Name is uid 4's Task Name cell");
    gate::check(text_of(inspector->outline_field()) == QStringLiteral("1.1.2"),
                "the pane shows uid 4's stored OutlineNumber");
    gate::check(text_of(inspector->type_field()) == QStringLiteral("Task"),
                "uid 4 is a plain Task");
    {
        msqt::TaskInformationValues info;
        gate::check(window.task_information_values(&info),
                    "uid 4's Task Information values are readable");
        const QString expected = info.manually_scheduled
                                     ? QStringLiteral("Manually Scheduled")
                                     : QStringLiteral("Auto Scheduled");
        gate::check(text_of(inspector->mode_field()) == expected,
                    "the pane's Mode is the stored scheduling mode");
    }
    gate::check(text_of(inspector->start_field()) ==
                    QString::fromStdString(
                        sheet_text(window, 4, msqt::entry_field(msqt::EntryCellKind::start))),
                "the pane's Start is the sheet's Start cell");
    gate::check(text_of(inspector->finish_field()) ==
                    QString::fromStdString(
                        sheet_text(window, 4, msqt::entry_field(msqt::EntryCellKind::finish))),
                "the pane's Finish is the sheet's Finish cell");
    gate::check(text_of(inspector->duration_field()) == QStringLiteral("2 days"),
                "the pane shows uid 4's two-day duration");
    gate::check(text_of(inspector->percent_field()) == QStringLiteral("0%"),
                "the pane shows uid 4's stored 0 %");
    gate::check(text_of(inspector->critical_field()) == QStringLiteral("No"),
                "uid 4 is not stored critical");
    gate::check(text_of(inspector->calendar_field()) == QStringLiteral("Project calendar"),
                "uid 4 has no task calendar");
    gate::check(text_of(inspector->predecessors_field()) ==
                    QStringLiteral("3FS+2 days"),
                "the pane shows uid 4's COM-measured predecessor text");
    gate::check(text_of(inspector->successors_field()) == QStringLiteral("6FF"),
                "the pane shows uid 4's COM-measured successor text");
    gate::check(text_of(inspector->resources_field()).isEmpty(),
                "uid 4 is unassigned");

    // --- 4. the pane keeps following the selection while it stays open -----
    gate::check(click_task(window, 1), "uid 1 has a sheet row");
    gate::check(window.inspector_visible(), "the pane stays open across the click");
    gate::check(text_of(inspector->id_field()) == QStringLiteral("1") &&
                    text_of(inspector->type_field()) == QStringLiteral("Summary") &&
                    text_of(inspector->outline_field()) == QStringLiteral("1"),
                "the pane reports uid 1 as the level-1 Summary");
    gate::check(text_of(inspector->predecessors_field()).isEmpty() &&
                    text_of(inspector->successors_field()).isEmpty(),
                "uid 1 has no stored links of its own");
    gate::check(click_task(window, 6), "uid 6 has a sheet row");
    gate::check(text_of(inspector->predecessors_field()) == QStringLiteral("4FF"),
                "the pane shows uid 6's COM-measured predecessor text");
    gate::check(text_of(inspector->resources_field()) == QStringLiteral("Resource A"),
                "the pane shows uid 6's assigned resource");

    // --- 5. the pane is a toggle (no View menu check box exists) -----------
    gate::check(click_inspect(window), "the Inspect button is still live");
    gate::check(!window.inspector_visible() && !inspector->isVisible(),
                "a second Inspect click hides the pane");
    gate::check(click_inspect(window), "the Inspect button is still live");
    gate::check(window.inspector_visible() && inspector->isVisible(),
                "a third Inspect click shows it again");
    gate::check(text_of(inspector->id_field()) == QStringLiteral("6"),
                "re-showing the pane refreshes the current selection");

    // --- 6. the project summary row is presented from the project facts ----
    gate::check(show_project_summary(window), "the project summary row is shown");
    sheet = window.sheet();  // the toggle rebuilds the displayed rows
    gate::check(sheet != nullptr && row_for_uid(window, 0) == 0,
                "the project summary row is row 0");
    gate::check(click_task(window, 0), "the project summary row is clickable");
    gate::check(text_of(inspector->id_field()) == QStringLiteral("0") &&
                    text_of(inspector->uid_field()) == QStringLiteral("0"),
                "the project row is ID/UniqueID 0");
    gate::check(text_of(inspector->type_field()) == QStringLiteral("Project summary"),
                "the pane names the synthetic project row");
    gate::check(text_of(inspector->calendar_field()) == QStringLiteral("Project calendar"),
                "the project row uses the project calendar");
    gate::check(text_of(inspector->name_field()) ==
                    QString::fromStdString(
                        sheet_text(window, 0, msqt::entry_field(msqt::EntryCellKind::name))),
                "the project row's Name is the sheet's project name");
    gate::check(text_of(inspector->start_field()) ==
                    QString::fromStdString(
                        sheet_text(window, 0, msqt::entry_field(msqt::EntryCellKind::start))),
                "the project row's Start is the sheet's project start");
    gate::check(text_of(inspector->finish_field()) ==
                    QString::fromStdString(
                        sheet_text(window, 0, msqt::entry_field(msqt::EntryCellKind::finish))),
                "the project row's Finish is the sheet's project finish");
    gate::check(text_of(inspector->duration_field()) ==
                    QString::fromStdString(sheet_text(
                        window, 0, msqt::entry_field(msqt::EntryCellKind::duration))),
                "the project row's Duration is the sheet's project duration");
    gate::check(!window.document_dirty(),
                "showing the project row and inspecting it leave the document clean");

    // --- 7. the preference survives a view switch --------------------------
    trigger(window, app::MenuAction::view_resource_sheet);
    gate::check(!inspector->isVisible(),
                "another view hides the Task Inspector (it is a task-view pane)");
    trigger(window, app::MenuAction::view_task_sheet);
    gate::check(window.inspector_visible() && inspector->isVisible(),
                "returning to the Task Sheet re-docks the pane");
    gate::check(!window.document_dirty(), "the view switches never dirty the document");

    // --- 8. the pane's own Close button hides it ---------------------------
    gate::check(inspector->close_button() != nullptr, "the pane carries a Close button");
    inspector->close_button()->click();
    QApplication::processEvents();
    gate::check(!window.inspector_visible() && !inspector->isVisible(),
                "the pane's Close button hides it");

    // --- 9. a pending Insert Task row: no invented stored facts ------------
    {
        msqt::MainWindow insert_window;
        insert_window.resize(msqt::kParityWidth, 800);
        QString insert_error;
        gate::check(insert_window.open_file(fixture, &insert_error),
                    "the fixture loads in the second window");
        insert_window.show();
        QApplication::processEvents();
        QApplication::processEvents();
        core::sheet::SheetModel* insert_sheet = insert_window.sheet();
        gate::check(insert_sheet != nullptr, "the second window has a sheet");
        if (insert_sheet != nullptr) {
            const std::set<std::uint32_t> before = uids_in_sheet(insert_window);
            gate::check(click_task(insert_window, 6),
                        "uid 6 is selected so the insert lands below it");
            InputDialogResult seen;
            accept_input_dialog(&seen, QStringLiteral("Inspector Pending Row"));
            trigger(insert_window, app::MenuAction::insert_task);
            gate::check(seen.seen, "Insert Task asks for a name");
            insert_sheet = insert_window.sheet();  // the insert rebuilds the rows
            std::uint32_t pending_uid = 0;
            for (std::uint32_t uid : uids_in_sheet(insert_window)) {
                if (before.find(uid) == before.end()) {
                    pending_uid = uid;
                }
            }
            gate::check(pending_uid != 0, "the pending insert has a displayed row");
            msqt::TaskInspector* insert_inspector = insert_window.task_inspector();
            gate::check(insert_inspector != nullptr, "the second window has the pane");
            if (pending_uid != 0 && insert_inspector != nullptr) {
                gate::check(click_task(insert_window, pending_uid),
                            "the pending insert row is clickable");
                if (!insert_window.inspector_visible() &&
                    inspect_button(insert_window) != nullptr) {
                    inspect_button(insert_window)->click();
                    QApplication::processEvents();
                }
                gate::check(insert_window.inspector_visible(),
                            "the pane opens on the pending insert row");
                gate::check(text_of(insert_inspector->type_field()) ==
                                QStringLiteral("Task (not saved yet)"),
                            "the pane says the pending insert is not saved yet");
                gate::check(text_of(insert_inspector->mode_field()) ==
                                QStringLiteral("not saved yet"),
                            "the pane does not invent a stored mode for a pending row");
                gate::check(text_of(insert_inspector->name_field()) ==
                                QStringLiteral("Inspector Pending Row"),
                            "the pending row's typed name is shown");
                gate::check(text_of(insert_inspector->start_field()) ==
                                QString::fromStdString(sheet_text(
                                    insert_window, pending_uid,
                                    msqt::entry_field(msqt::EntryCellKind::start))),
                            "the pending row's Start is the value the sheet shows");
                gate::check(insert_window.pending_start_edits() == 0,
                            "inspecting a pending row stages no start edit");
            }
        }
    }

    gate::check(!window.document_dirty(),
                "the whole inspection run leaves the first document clean");

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(window.grab().save(out_dir + QStringLiteral("/task-inspector.png"), "PNG"),
                    "the inspector view is written for human review");
    }

    return gate::finish();
}
