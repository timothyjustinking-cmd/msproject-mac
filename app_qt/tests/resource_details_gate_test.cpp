// app_qt/tests/resource_details_gate_test.cpp -- wave 30 (W30-2.2).
//
// THE TARGET (wave 30 brief, G2): the Resource tab's Details opens Project's
// Resource Form.  The Resource Sheet is read-only in this port, so the pane is
// a VIEW of app::PlanInput's resource and assignment facts:
//   * the live Details button is enabled, wired to
//     RibbonCommand::resource_details, and a click switches to the Resource
//     Sheet and docks the pane in the Details splitter;
//   * every field is read-only, and opening, following, switching views and
//     re-showing it never dirty the document;
//   * the pane follows a click on a Resource Sheet row by UID and shows the
//     plan's own ID, Unique ID, Type, Initials, Max Units, Work (the sum of
//     the resource's assignment work) and assignment count;
//   * the Details preference is shared with the Task Form: exactly one form is
//     visible for the active view (Task Sheet -> Task Form, Resource Sheet ->
//     Resource Form), usage views show neither, and a switch back re-docks the
//     matching form;
//   * a freshly shown Resource Sheet starts with no selected resource, so the
//     pane cannot carry a stale row from another view.
//
// THE FIXTURE: tests/data/wave23-outline.mpp -- the same synthetic file the
// Task Inspector gate uses.  Its one resource is COM-measured: UniqueID/ID 1,
// "Resource A", initials "R", Work type, 100 % max units, assigned once to
// uid 6 for 2 days of work (task_order_test pins the assignment texts).
//
// Usage: qt_resource_details_gate <fixture.mpp> [out-dir]
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
#include <QTableView>
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

#include <cstdio>
#include <string>

#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "resource_form.h"
#include "ribbon_bar.h"
#include "shell_style.h"
#include "task_form.h"

namespace {

QString text_of(const QLineEdit* field) {
    return field != nullptr ? field->text() : QString();
}

QAbstractButton* details_button(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int resource_tab = ribbon->tab_titles().indexOf(QStringLiteral("Resource"));
    if (resource_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(resource_tab);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int properties = groups.indexOf(QStringLiteral("Properties"));
    if (properties < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(properties); ++control) {
        auto* button =
            qobject_cast<QAbstractButton*>(ribbon->group_control(properties, control));
        if (button != nullptr && button->text() == QStringLiteral("Details")) {
            return button;
        }
    }
    return nullptr;
}

// Clicks the LIVE Details button.  RibbonBar::set_active_tab() (inside
// details_button) rebuilds the page and deletes its controls, so a pointer
// cached across a tab switch is stale: find it again on every click.
bool click_details(msqt::MainWindow& window) {
    QAbstractButton* button = details_button(window);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

int column_titled(QAbstractItemModel* model, const QString& title) {
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() == title) {
            return column;
        }
    }
    return -1;
}

// Selects the resource-sheet row with a real mouse click, so the clicked
// handler (and the pane refresh it performs) runs exactly as it does live.
bool click_resource_row(msqt::MainWindow& window, int row, int column) {
    QAbstractItemModel* model = window.sheet_view()->model();
    if (model == nullptr || row < 0 || row >= model->rowCount() || column < 0) {
        return false;
    }
    const QModelIndex index = model->index(row, column);
    window.sheet_view()->scrollTo(index);
    QApplication::processEvents();
    const QRect rect = window.sheet_view()->visualRect(index);
    if (!rect.isValid()) {
        return false;
    }
    QTest::mousePress(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      rect.center());
    QTest::mouseRelease(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                        rect.center());
    QApplication::processEvents();
    return true;
}

void trigger(msqt::MainWindow& window, app::MenuAction id) {
    window.refresh_menu_state();
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, "the menu carries the action");
    if (action != nullptr) {
        action->trigger();
        QApplication::processEvents();
        QApplication::processEvents();
    }
}

bool all_fields_read_only(msqt::ResourceForm* form) {
    if (form == nullptr) {
        return false;
    }
    const QLineEdit* fields[] = {
        form->name_field(),     form->id_field(),       form->uid_field(),
        form->type_field(),     form->initials_field(), form->max_units_field(),
        form->work_field(),     form->assignments_field(),
    };
    for (const QLineEdit* field : fields) {
        if (field == nullptr || !field->isReadOnly()) {
            return false;
        }
    }
    return true;
}

bool fields_empty(msqt::ResourceForm* form) {
    if (form == nullptr) {
        return false;
    }
    return text_of(form->name_field()).isEmpty() &&
           text_of(form->id_field()).isEmpty() &&
           text_of(form->uid_field()).isEmpty() &&
           text_of(form->type_field()).isEmpty() &&
           text_of(form->initials_field()).isEmpty() &&
           text_of(form->max_units_field()).isEmpty() &&
           text_of(form->work_field()).isEmpty() &&
           text_of(form->assignments_field()).isEmpty();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_resource_details_gate <fixture.mpp> [out-dir]\n");
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

    msqt::ResourceForm* form = window.resource_form();
    msqt::TaskForm* task_form = window.task_form();
    gate::check(form != nullptr, "the window carries the Resource Form pane");
    gate::check(task_form != nullptr, "the window carries the Task Form pane");
    if (form == nullptr || task_form == nullptr) {
        return gate::finish();
    }

    // --- 1. the live ribbon button and its wiring --------------------------
    QAbstractButton* details = details_button(window);
    gate::check(details != nullptr, "the Resource tab's Properties group carries Details");
    gate::check(details != nullptr && details->isEnabled(), "Details is live (W30-2.2)");
    gate::check(details != nullptr && details->toolTip() == QStringLiteral("Details"),
                "the enabled Details button carries its live tooltip");
    if (details == nullptr) {
        return gate::finish();
    }
    {
        msqt::RibbonBar* ribbon = window.ribbon();
        const QStringList groups = ribbon->group_titles();
        const int properties = groups.indexOf(QStringLiteral("Properties"));
        bool wired = false;
        for (int control = 0; control < ribbon->group_control_count(properties); ++control) {
            if (ribbon->group_control(properties, control) == details) {
                wired = ribbon->group_control_command(properties, control) ==
                        msqt::RibbonCommand::resource_details;
                break;
            }
        }
        gate::check(wired, "the live button is wired to RibbonCommand::resource_details");
    }
    gate::check(!window.details_visible(), "the Details pane starts hidden");
    gate::check(!form->isVisible(), "the hidden Resource Form is not on screen");
    gate::check(all_fields_read_only(form),
                "every Resource Form field is read-only (no write path)");
    gate::check(form->selection_label()->text() == QStringLiteral("No resource selected"),
                "the pane starts by saying no resource is selected");

    // --- 2. Details switches to the Resource Sheet and docks the pane ------
    window.sheet_view()->setCurrentIndex(QModelIndex());
    gate::check(click_details(window), "the Details button is still live");
    gate::check(window.view_mode() == app::ViewMode::resource_sheet,
                "Details switches to the Resource Sheet the form belongs to");
    gate::check(window.details_visible() && form->isVisible(),
                "Details docks the Resource Form");
    gate::check(!task_form->isVisible(),
                "the Task Form is not shown beside the Resource Form");
    gate::check(form->selection_label()->text() == QStringLiteral("No resource selected"),
                "a freshly shown Resource Sheet selects no resource");
    gate::check(fields_empty(form), "the empty pane clears every field");
    gate::check(!window.document_dirty(), "showing the pane does not dirty the document");

    // --- 3. a click on the resource row fills the pane from the plan -------
    auto* model = window.sheet_view()->model();
    gate::check(model != nullptr && model->rowCount() == 1,
                "the fixture's Resource Sheet has one row");
    const int name_column = column_titled(model, QStringLiteral("Resource Name"));
    gate::check(name_column >= 0, "the Resource Sheet shows the Resource Name column");
    gate::check(click_resource_row(window, 0, name_column), "the resource row is clickable");
    gate::check(text_of(form->name_field()) == QStringLiteral("Resource A"),
                "the pane shows the plan's resource name");
    gate::check(model != nullptr && name_column >= 0 &&
                    text_of(form->name_field()) ==
                        model->index(0, name_column).data(Qt::DisplayRole).toString(),
                "the pane's Name is the Resource Name cell");
    gate::check(text_of(form->id_field()) == QStringLiteral("1"),
                "the pane shows the resource's ID");
    gate::check(text_of(form->uid_field()) == QStringLiteral("1"),
                "the pane shows the resource's Unique ID");
    gate::check(text_of(form->type_field()) == QStringLiteral("Work"),
                "the pane shows the resource's Work type");
    gate::check(text_of(form->initials_field()) == QStringLiteral("R"),
                "the pane shows the resource's stored initials");
    gate::check(text_of(form->max_units_field()) == QStringLiteral("100%"),
                "the pane shows the resource's 100 % max units");
    gate::check(text_of(form->work_field()) == QStringLiteral("2 days"),
                "the pane totals the resource's assignment work (2 days)");
    gate::check(text_of(form->assignments_field()) == QStringLiteral("1"),
                "the pane counts the resource's one assignment");
    gate::check(form->selection_label()->text() ==
                    QStringLiteral("Resource ID 1 (UniqueID 1)"),
                "the pane names the selected resource");
    gate::check(!window.document_dirty(), "following a resource row never dirties the file");

    // --- 4. the Details preference is shared, one form per view -----------
    trigger(window, app::MenuAction::view_task_sheet);
    gate::check(window.view_mode() == app::ViewMode::task_sheet,
                "the View menu returns to the Task Sheet");
    gate::check(!form->isVisible(), "the Resource Sheet's pane is hidden in the Task Sheet");
    gate::check(window.details_visible() && task_form->isVisible(),
                "the shared Details preference re-docks the Task Form");
    trigger(window, app::MenuAction::view_resource_sheet);
    gate::check(window.view_mode() == app::ViewMode::resource_sheet,
                "the View menu returns to the Resource Sheet");
    gate::check(!task_form->isVisible(), "the Task Form is hidden again");
    gate::check(window.details_visible() && form->isVisible(),
                "the Resource Form is re-docked by the same preference");
    gate::check(form->selection_label()->text() == QStringLiteral("No resource selected"),
                "the re-shown Resource Sheet has no stale selection");
    gate::check(fields_empty(form), "the re-shown pane clears the previous resource");
    gate::check(!window.document_dirty(), "the view switches never dirty the document");

    // --- 5. a usage view owns neither form ---------------------------------
    trigger(window, app::MenuAction::view_resource_usage);
    gate::check(window.view_mode() == app::ViewMode::resource_usage,
                "the View menu switches to Resource Usage");
    gate::check(!form->isVisible() && !task_form->isVisible(),
                "a usage view shows neither details form");
    trigger(window, app::MenuAction::view_resource_sheet);
    gate::check(window.details_visible() && form->isVisible(),
                "returning to the Resource Sheet re-docks the Resource Form");

    // --- 6. Details is a SHOW command, not a toggle ------------------------
    model = window.sheet_view()->model();
    const int name_again = column_titled(model, QStringLiteral("Resource Name"));
    gate::check(click_resource_row(window, 0, name_again),
                "the resource row is clickable after the switches");
    gate::check(text_of(form->name_field()) == QStringLiteral("Resource A"),
                "the pane again follows the resource row");
    gate::check(click_details(window), "the Details button is still live");
    gate::check(window.details_visible() && form->isVisible(),
                "a second Details click keeps the pane shown");
    gate::check(text_of(form->name_field()) == QStringLiteral("Resource A"),
                "re-showing the pane keeps the current resource");
    gate::check(all_fields_read_only(form),
                "the fields stay read-only after every interaction");
    gate::check(!window.document_dirty(),
                "the whole Details run leaves the document clean");

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(window.grab().save(out_dir + QStringLiteral("/resource-details.png"),
                                       "PNG"),
                    "the Resource Form view is written for human review");
    }

    return gate::finish();
}
