// app_qt/tests/split_view_gate_test.cpp -- wave 26 (W26-6.3).
//
// THE TARGET (operator decision 16, docs/ui-parity-spec.md)
//   View > Details docks the Task Form at the bottom, synchronised with the
//   selection, in a resizable splitter.
//
// WHAT THIS GATE DOES (all through live widgets)
//   1. opens a committed fixture and proves the Details check box starts
//      UNCHECKED (Project's measured default) with the form hidden;
//   2. finds the LIVE check box on the View tab's Split View group and clicks
//      it: the form docks BELOW the sheet/Gantt pair, inside a vertical
//      QSplitter with a draggable handle;
//   3. selects a different task in the LIVE sheet with a real mouse click and
//      proves the form's fields follow the selection (same task names as the
//      sheet's own model, row by row);
//   3b. (wave 28, W28-4.1) commits the form's Name, Start and % Complete fields
//      and proves each reaches the same edit model the sheet uses (a sheet cell
//      session for Name/Start, the measured set_percent edit for percent), that
//      the two fields the sheet declares read-only stay disabled, and that
//      Edit > Undo reverses every one of them with the form following;
//   4. resizes the splitter and proves the pane's height follows, then clicks
//      Details again and proves the pane is gone and the document is not dirty.
//
// Usage: qt_split_view_gate <fixture.mpp> [out-dir]
#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QHeaderView>
#include <QImage>
#include <QLineEdit>
#include <QModelIndex>
#include <QSplitter>
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
#include <QToolButton>

#include <cstdio>

#include "gantt_canvas.h"
#include "gate_util.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "shell_style.h"
#include "task_form.h"

namespace {

// The Details check box on the View tab's Split View group, found by walking the
// LIVE control inventory (never by object name or a private pointer).
QCheckBox* details_check_box(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int view_tab = ribbon->tab_titles().indexOf(QStringLiteral("View"));
    if (view_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(view_tab);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int split_group = groups.indexOf(QStringLiteral("Split View"));
    if (split_group < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(split_group); ++control) {
        auto* box = qobject_cast<QCheckBox*>(ribbon->group_control(split_group, control));
        if (box != nullptr && box->text() == QStringLiteral("Details")) {
            return box;
        }
    }
    return nullptr;
}

// W30-1.5: the Task tab's Details button, found through the LIVE control
// inventory (the same rule as the View check box above).
QAbstractButton* task_details_button(msqt::MainWindow& window) {
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

int name_column(QAbstractItemModel* model) {
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() ==
            QStringLiteral("Task Name")) {
            return column;
        }
    }
    return -1;
}

QString sheet_name(msqt::MainWindow& window, int row) {
    QAbstractItemModel* model = window.sheet_view()->model();
    if (model == nullptr) {
        return QString();
    }
    const int column = name_column(model);
    return column < 0 ? QString() : model->index(row, column).data().toString();
}

void click_sheet_row(msqt::MainWindow& window, int row) {
    QAbstractItemModel* model = window.sheet_view()->model();
    const int column = name_column(model);
    const QModelIndex index = model->index(row, column < 0 ? 0 : column);
    window.sheet_view()->scrollTo(index);
    QApplication::processEvents();
    const QRect rect = window.sheet_view()->visualRect(index);
    // Press + release, as the dialog gate does: the (Qt 6.5, MSVC /W4 /WX)
    // qtestmouse.h template's mouseClick overload trips MSVC C4701.
    QTest::mousePress(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      rect.center());
    QTest::mouseRelease(window.sheet_view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                        rect.center());
    QApplication::processEvents();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_split_view_gate <fixture.mpp> [out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    msqt::MainWindow window;
    window.resize(1200, 800);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    msqt::TaskForm* form = window.task_form();
    // W30-2.2 added the Resource Sheet's read-only form as a third pane; the
    // two forms share the Details preference and only one is ever visible.
    msqt::ResourceForm* resource_form = window.resource_form();
    QSplitter* outer = window.details_splitter();
    gate::check(form != nullptr, "the window owns a Task Form pane");
    gate::check(resource_form != nullptr, "the window owns a Resource Form pane");
    gate::check(outer != nullptr, "the window owns the Details splitter");
    if (form == nullptr || resource_form == nullptr || outer == nullptr) {
        return gate::finish();
    }
    gate::check(outer->orientation() == Qt::Vertical,
                "Details docks in a VERTICAL splitter");
    gate::check(outer->count() == 3,
                "the splitter has the view pair and one form per sheet");
    gate::check(outer->widget(0) == window.splitter(),
                "the sheet/Gantt pair stays the splitter's first pane");
    gate::check(outer->widget(1) == form, "the Task Form is the second pane");
    gate::check(outer->widget(2) == resource_form,
                "the Resource Form is the third pane");
    gate::check(outer->handleWidth() == msqt::kSplitHandleWidth,
                "the splitter handle is the measured 6-px width");
    gate::check(outer->handle(1) != nullptr, "the splitter has a draggable handle");

    // --- 1. Project's default: unchecked and undocked -----------------------
    QCheckBox* details = details_check_box(window);
    gate::check(details != nullptr, "the View tab carries the Details check box");
    gate::check(details != nullptr && details->isEnabled(),
                "the Details check box is live (W26-6.3)");
    gate::check(!window.details_visible(), "Details starts unchecked");
    gate::check(form->isHidden(), "the Task Form starts undocked");
    gate::check(!window.document_dirty(), "the fixture opens clean");

    QAbstractItemModel* model = window.sheet_view()->model();
    gate::check(model != nullptr && model->rowCount() >= 2,
                "the fixture has at least two rows to select between");
    const QString first_name = sheet_name(window, 0);
    const QString second_name = sheet_name(window, 1);
    gate::check(!first_name.isEmpty() && !second_name.isEmpty(),
                "the sheet names the first two rows");
    gate::check(first_name != second_name, "the two rows are different tasks");

    // --- 2. click the live box: the form docks below the Gantt --------------
    if (details != nullptr) {
        details->click();
        QApplication::processEvents();
        QApplication::processEvents();
    }
    gate::check(window.details_visible(), "clicking Details docks the pane");
    gate::check(!form->isHidden(), "the Task Form is visible");
    gate::check(details != nullptr && details->isChecked(),
                "the live check box reads checked");
    const int form_top = form->mapTo(&window, QPoint(0, 0)).y();
    const int gantt_bottom =
        window.gantt_view()
            ->mapTo(&window, QPoint(0, window.gantt_view()->height()))
            .y();
    gate::check(form_top >= gantt_bottom - 2,
                "the Task Form is docked BELOW the Gantt");
    gate::check(form->height() >= 100, "the docked pane has a usable height");
    gate::check(form->name_field() != nullptr &&
                    form->name_field()->text() == first_name,
                "the docked form shows the selected task's name");
    gate::check(!form->duration_field()->text().isEmpty(),
                "the docked form shows the task's duration");
    gate::check(!form->start_field()->text().isEmpty(),
                "the docked form shows the task's start");

    // --- 3. the form follows the selection ---------------------------------
    click_sheet_row(window, 1);
    gate::check(form->name_field()->text() == second_name,
                "selecting row 2 updates the Task Form's name");
    click_sheet_row(window, 0);
    gate::check(form->name_field()->text() == first_name,
                "selecting row 1 again updates it back");

    // --- 3b. W28-4.1: the form's fields edit through the sheet's own paths --
    // Each field reports its value to MainWindow, which hands it to the SAME
    // apply_task_information path the sheet cells and Task Information use:
    // a begin_edit/commit_edit sheet session for the four cell fields and the
    // measured set_percent structural edit for % Complete.  The form adds no
    // writer of its own; the two fields the sheet declares read-only stay
    // disabled rather than offering an edit a save would drop.
    gate::check(!form->name_field()->isReadOnly(),
                "the form's Name field is editable");
    gate::check(!form->duration_field()->isReadOnly(),
                "the form's Duration field is editable");
    gate::check(!form->start_field()->isReadOnly(),
                "the form's Start field is editable");
    gate::check(!form->finish_field()->isReadOnly(),
                "the form's Finish field is editable");
    gate::check(!form->percent_field()->isReadOnly(),
                "the form's % Complete field is editable");
    gate::check(form->predecessors_field()->isReadOnly() &&
                    !form->predecessors_field()->isEnabled(),
                "Predecessors stays disabled (the sheet declares it read-only)");
    gate::check(form->resources_field()->isReadOnly() &&
                    !form->resources_field()->isEnabled(),
                "Resources stays disabled (no assignment writer)");

    QAction* undo = gate::find_action(window, app::MenuAction::undo);
    gate::check(undo != nullptr, "the Edit menu carries Undo");

    // Name: a sheet cell edit (in memory, exactly like the sheet's own Name
    // column), undone by the sheet's undo stack.
    const QString original_name = form->name_field()->text();
    const QString renamed = original_name + QStringLiteral(" (form)");
    form->name_field()->setFocus();
    form->name_field()->setText(renamed);
    QTest::keyClick(form->name_field(), Qt::Key_Return);
    QApplication::processEvents();
    gate::check(sheet_name(window, 0) == renamed,
                "Return commits the form's Name through the sheet model");
    gate::check(form->name_field()->text() == renamed,
                "the form shows the committed name");
    if (undo != nullptr) {
        undo->trigger();
        QApplication::processEvents();
    }
    gate::check(sheet_name(window, 0) == original_name,
                "Edit > Undo reverses the form's Name edit");
    gate::check(form->name_field()->text() == original_name,
                "the form follows the Name undo");

    // Start: the one cell field the save path persists.  The form's edit is
    // the same pending start the sheet's in-cell editor makes.
    gate::check(window.pending_start_edits() == 0,
                "the fixture opens with no pending start edit");
    const QString original_start = form->start_field()->text();
    form->start_field()->setFocus();
    form->start_field()->setText(QStringLiteral("2026-02-02"));
    QTest::keyClick(form->start_field(), Qt::Key_Return);
    QApplication::processEvents();
    gate::check(window.pending_start_edits() == 1,
                "the form's Start edit is one pending save edit");
    gate::check(window.document_dirty(), "the document is dirty after the Start edit");
    gate::check(form->start_field()->text() != original_start,
                "the form shows the new start");
    if (undo != nullptr) {
        undo->trigger();
        QApplication::processEvents();
    }
    gate::check(window.pending_start_edits() == 0,
                "Edit > Undo reverses the form's Start edit");
    gate::check(form->start_field()->text() == original_start,
                "the form follows the Start undo");

    // % Complete: no sheet cell path (that column is read-only); the form
    // queues the same measured set_percent edit the ribbon's % buttons and Task
    // Information queue.
    const QString original_percent = form->percent_field()->text();
    // A value that differs from the row's stored percent (the fixture stores
    // 0 %; this also keeps the gate honest if it ever changes).  W29-0.0
    // (R28-2): only the measured transitions are enabled -- automatic 0->40,
    // 40->100, 100->0 and manual 0->40 -- so the typed value is 40, not 25/50.
    QString original_digits;
    for (const QChar ch : original_percent) {
        if (ch.isDigit()) {
            original_digits += ch;
        }
    }
    const int typed_percent_value = original_digits.toInt() == 40 ? 100 : 40;
    const QString typed_percent = QString::number(typed_percent_value) + QStringLiteral("%");
    const std::size_t pending_before_percent = window.pending_edits().size();
    form->percent_field()->setFocus();
    form->percent_field()->setText(typed_percent);
    QTest::keyClick(form->percent_field(), Qt::Key_Return);
    QApplication::processEvents();
    gate::check(window.pending_edits().size() == pending_before_percent + 1,
                "the form's % Complete edit queues exactly one structural edit");
    const app::StructuralEdit* queued = window.pending_edits().next_undo();
    gate::check(queued != nullptr &&
                    queued->kind == app::StructuralKind::set_percent &&
                    queued->percent == typed_percent_value,
                "the queued edit is the measured set_percent");
    gate::check(form->percent_field()->text() == typed_percent,
                "the form shows the percent it queued");
    if (undo != nullptr) {
        undo->trigger();
        QApplication::processEvents();
    }
    gate::check(window.pending_edits().size() == pending_before_percent,
                "Edit > Undo drops the form's percent edit");
    gate::check(form->percent_field()->text() != typed_percent,
                "the form follows the percent undo");
    gate::check(!window.document_dirty(),
                "the form's undone edits leave the document clean");

    // An unchanged field is not a file change and not an undo step.
    const QString unchanged = form->name_field()->text();
    form->name_field()->setFocus();
    form->name_field()->setText(unchanged);
    QTest::keyClick(form->name_field(), Qt::Key_Return);
    QApplication::processEvents();
    gate::check(window.sheet() != nullptr && !window.sheet()->can_undo(),
                "Return on the unchanged value queues no edit");

    // --- 4. the splitter is resizable; the toggle undocks ------------------
    const int total = outer->height();
    outer->setSizes(QList<int>{total * 3 / 4, total / 4});
    QApplication::processEvents();
    const int after_small = form->height();
    outer->setSizes(QList<int>{total / 2, total / 2});
    QApplication::processEvents();
    const int after_big = form->height();
    gate::check(after_big > after_small,
                "dragging/setting the splitter resizes the Task Form pane");
    gate::check(after_small >= 100, "the smaller pane stays usable");
    outer->setSizes(QList<int>{total * 3 / 4, total / 4});
    QApplication::processEvents();
    gate::check(form->height() >= 100, "the split pane is restored after a resize");

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(window.grab().save(
                        out_dir + QStringLiteral("/split-view-details.png"), "PNG"),
                    "the docked split view is written for human review");
    }

    if (details != nullptr) {
        details->click();
        QApplication::processEvents();
    }
    gate::check(!window.details_visible(), "clicking Details again undocks it");
    gate::check(form->isHidden(), "the Task Form is hidden again");
    gate::check(!window.document_dirty(),
                "toggling Details never marks the document for saving");

    // --- 5. the Task tab's Details shows the same pane (W30-1.5) -----------
    QAbstractButton* task_details = task_details_button(window);
    gate::check(task_details != nullptr, "the Task tab carries the Details button");
    gate::check(task_details != nullptr && task_details->isEnabled(),
                "the Task tab's Details is live (W30-1.5)");
    if (task_details != nullptr) {
        task_details->click();
        QApplication::processEvents();
        QApplication::processEvents();
    }
    gate::check(window.details_visible(), "the Task tab's Details docks the pane");
    gate::check(!form->isHidden(), "the Task Form is visible again");
    gate::check(!window.document_dirty(),
                "opening Details never marks the document for saving");
    QCheckBox* after_task = details_check_box(window);
    gate::check(after_task != nullptr && after_task->isChecked(),
                "the View tab's Details check reflects the open pane");

    return gate::finish();
}
