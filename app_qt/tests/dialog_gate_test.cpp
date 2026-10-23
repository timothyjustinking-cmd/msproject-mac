// app_qt/tests/dialog_gate_test.cpp -- wave 20's per-screen gate for each dialog
// the shell implements or touches.
//
// The brief's requirement is "fixed size, margins/spacing, control geometry --
// values measured from the widget tree, not guessed".  Each dialog therefore:
//   * is its own QDialog class with a pinned setFixedSize() and layout constants
//     (app_qt/dialogs.h dialog_geometry),
//   * is constructed and SHOWN here (offscreen) WITHOUT exec(),
//   * has its size, minimum==maximum, contents margins, spacing, every form
//     label/field geometry and every button measured from the live widgets.
// The test also drives each dialog's accept/result path over the portable
// app/ model, and proves Preferences persists through PreferencesStore and
// applies to the loaded sheet (date/duration/zoom/shading).
//
// Screens gated: Preferences, Table / Date Format, Custom Field Formula,
// Task Fields, Predecessors, Filter.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMargins>
#include <QMetaObject>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
// Qt 6.5.3's qtestmouse.h trips MSVC /W4 C4701 (an uninitialised local in the
// inline double-click helper).  The gate builds with /WX, so scope the warning
// off for this third-party header only.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4701)
#endif
#include <QTest>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "dialogs.h"
#include "entry_columns.h"
#include "filter_model.h"  // app::FilterField / app::filter_operators()
#include "gantt_canvas.h"
#include "main_window.h"
#include "preferences_store.h"
#include "shell_style.h"

#include "gate_util.h"

namespace {

using namespace msqt::dialog_geometry;

QWidget* item_widget(QFormLayout* form, int row, QFormLayout::ItemRole role) {
    if (form == nullptr || row < 0) {
        return nullptr;
    }
    QLayoutItem* item = form->itemAt(row, role);
    return item != nullptr ? item->widget() : nullptr;
}

void check_button(QPushButton* button, const char* what) {
    gate::check(button != nullptr, what);
    if (button == nullptr) {
        return;
    }
    gate::check(button->isVisible(), "the button is visible");
    gate::check(button->height() == kButtonHeight, "the button has the pinned height");
    gate::check(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 2,
                "the button fits its label");
}

void check_shell(QDialog& dialog, const char* name,
                 const msqt::dialog_geometry::DialogSize size) {
    gate::check(dialog.width() == size.width && dialog.height() == size.height,
                "the dialog has its fixed size");
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "the dialog is non-resizable (min == max)");
    QLayout* layout = dialog.layout();
    gate::check(layout != nullptr, "the dialog has an outer layout");
    if (layout == nullptr) {
        return;
    }
    gate::check(layout->contentsMargins() == QMargins(kMargin, kMargin, kMargin, kMargin),
                "the dialog uses the pinned 12 px margins");
    gate::check(layout->spacing() == kSpacing, "the dialog uses the pinned 8 px spacing");
    const QRect content = dialog.rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
    for (QWidget* child :
         dialog.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (child == nullptr || !child->isVisible()) {
            continue;
        }
        gate::check(gate::inside(content, child->geometry()),
                    "every direct child stays inside the dialog's content rectangle");
    }
    std::fprintf(stderr,
                 "note: %s %dx%d margins=%d/%d/%d/%d spacing=%d\n", name, dialog.width(),
                 dialog.height(), kMargin, kMargin, kMargin, kMargin, kSpacing);
}

void check_form(QDialog& dialog, const char* object_name, int rows) {
    QFormLayout* form = dialog.findChild<QFormLayout*>(QString::fromLatin1(object_name));
    gate::check(form != nullptr, "the dialog has its form layout");
    if (form == nullptr) {
        return;
    }
    gate::check(form->rowCount() == rows, "the form has the expected row count");
    const int count = std::min(form->rowCount(), rows);
    int previous_bottom = -1;
    for (int row = 0; row < count; ++row) {
        QWidget* label = item_widget(form, row, QFormLayout::LabelRole);
        QWidget* field = item_widget(form, row, QFormLayout::FieldRole);
        gate::check(label != nullptr && field != nullptr, "a form row has a label and a field");
        if (label == nullptr || field == nullptr) {
            continue;
        }
        gate::check(label->x() == kMargin, "the form label starts at the dialog margin");
        gate::check(label->width() == kLabelWidth, "the form label is the pinned 120 px column");
        gate::check(label->font().pointSize() == msqt::kBodyPointSize,
                    "the form label uses the pinned 11 pt body font");
        gate::check(field->x() == label->x() + label->width() + form->horizontalSpacing(),
                    "the field starts one spacing after the label column");
        if (qobject_cast<QCheckBox*>(field) != nullptr) {
            gate::check(field->height() == kCheckHeight, "the checkbox has the pinned height");
        } else {
            gate::check(field->width() == kFieldWidth,
                        "the form field is the pinned 280 px wide");
            gate::check(field->height() == kControlHeight,
                        "the form field is the pinned 24 px tall");
        }
        gate::check(field->y() >= previous_bottom, "form rows do not overlap");
        previous_bottom = field->y() + field->height();
    }
    std::fprintf(stderr, "note: form %s rows=%d label=%d field=%dx%d spacing=%d\n", object_name,
                 form->rowCount(), kLabelWidth, kFieldWidth, kControlHeight,
                 form->horizontalSpacing());
}

QDialogButtonBox* check_button_box(QDialog& dialog) {
    QDialogButtonBox* box = dialog.findChild<QDialogButtonBox*>();
    gate::check(box != nullptr, "the dialog has an OK/Cancel button box");
    if (box == nullptr) {
        return nullptr;
    }
    gate::check(box->width() == dialog.width() - 2 * kMargin,
                "the button box spans the content width");
    gate::check(box->y() + box->height() == dialog.height() - kMargin,
                "the button box sits on the bottom margin");
    QPushButton* ok = box->button(QDialogButtonBox::Ok);
    QPushButton* cancel = box->button(QDialogButtonBox::Cancel);
    gate::check(ok != nullptr && cancel != nullptr, "the box has OK and Cancel");
    if (ok != nullptr) {
        gate::check(ok->width() >= ok->fontMetrics().horizontalAdvance(ok->text()) + 2,
                    "OK fits its label");
    }
    std::fprintf(stderr, "note: buttonBox %dx%d at y=%d\n", box->width(), box->height(),
                 box->y());
    return box;
}

void check_bottom_button_row(QDialog& dialog, const std::vector<QPushButton*>& buttons) {
    gate::check(!buttons.empty(), "the dialog has its action buttons");
    for (QPushButton* button : buttons) {
        check_button(button, "a dialog action button exists");
    }
    if (buttons.empty()) {
        return;
    }
    QPushButton* last = buttons.back();
    if (last != nullptr) {
        gate::check(last->x() + last->width() == dialog.width() - kMargin,
                    "the button row ends on the right margin");
        gate::check(last->y() + last->height() == dialog.height() - kMargin,
                    "the button row sits on the bottom margin");
    }
}

void check_vertical_hint(QDialog& dialog, const char* object_name) {
    QLabel* hint = dialog.findChild<QLabel*>(QString::fromLatin1(object_name));
    if (hint == nullptr) {
        // Hints have no object name; the first wrapped QLabel is the one.
        for (QLabel* label : dialog.findChildren<QLabel*>()) {
            if (label->wordWrap()) {
                hint = label;
                break;
            }
        }
    }
    gate::check(hint != nullptr, "the dialog has an explanatory hint");
    if (hint != nullptr) {
        gate::check(hint->width() == dialog.width() - 2 * kMargin,
                    "the hint spans the content width");
        gate::check(!hint->text().isEmpty(), "the hint carries text");
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    const QString fixture = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString out_dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    // Wave 24 (W24-6): a fixture that stores the project row SHOWN, for the
    // real-double-click gate (the project row's own click must not open an
    // editable dialog).
    const QString project_fixture =
        argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
    }

    // -----------------------------------------------------------------------
    // Preferences: fixed shell + control geometry + the OK value round trip
    // -----------------------------------------------------------------------
    {
        app::Preferences current;
        current.date_style = app::DateStyle::day_month_year;
        msqt::PreferencesDialog dialog(current);
        dialog.show();
        QApplication::processEvents();
        check_shell(dialog, "Preferences", kPreferencesSize);
        check_form(dialog, "preferencesForm", 5);
        gate::check(dialog.date_style_combo() != nullptr &&
                        dialog.date_style_combo()->count() == 3,
                    "Preferences offers the three date styles");
        gate::check(dialog.duration_style_combo() != nullptr &&
                        dialog.duration_style_combo()->count() == 3,
                    "Preferences offers the three duration styles");
        gate::check(dialog.timescale_combo() != nullptr &&
                        dialog.timescale_combo()->count() == 3,
                    "Preferences offers the three timescales");
        gate::check(dialog.shading_check() != nullptr && dialog.shading_check()->isVisible(),
                    "Preferences has the shading checkbox");
        gate::check(dialog.theme_combo() != nullptr && dialog.theme_combo()->count() == 2,
                    "Preferences carries the Office theme choice");
        check_vertical_hint(dialog, "preferencesHint");
        check_button_box(dialog);
        dialog.date_style_combo()->setCurrentIndex(2);
        dialog.duration_style_combo()->setCurrentIndex(1);
        dialog.timescale_combo()->setCurrentIndex(0);
        dialog.shading_check()->setChecked(false);
        dialog.theme_combo()->setCurrentIndex(1);
        dialog.accept();
        const app::Preferences chosen = dialog.preferences();
        gate::check(chosen.date_style == app::DateStyle::iso, "OK returns the ISO date style");
        gate::check(chosen.duration_style == app::DurationStyle::hours,
                    "OK returns the hours duration style");
        gate::check(chosen.zoom == app::ZoomLevel::day, "OK returns the day timescale");
        gate::check(!chosen.working_day_shading, "OK returns shading off");
        gate::check(dialog.theme() == msqt::Theme::dark, "OK returns the dark theme");
    }

    // -----------------------------------------------------------------------
    // Table / Date Format
    // -----------------------------------------------------------------------
    {
        app::ViewStateEdits current;
        msqt::TableDateFormatDialog dialog(current);
        dialog.show();
        QApplication::processEvents();
        check_shell(dialog, "Table / Date Format", kTableDateFormatSize);
        check_form(dialog, "tableFormatForm", 5);
        gate::check(dialog.date_format_combo() != nullptr &&
                        dialog.date_format_combo()->count() == 2,
                    "the date-format combo offers formats 4 and 5");
        gate::check(dialog.column_index_spin() != nullptr &&
                        !dialog.column_index_spin()->isEnabled(),
                    "the column fields start disabled");
        dialog.replace_column_check()->setChecked(true);
        QApplication::processEvents();
        gate::check(dialog.column_index_spin()->isEnabled() &&
                        dialog.field_id_edit()->isEnabled() && dialog.title_edit()->isEnabled(),
                    "checking Replace enables the three column fields");
        dialog.date_format_combo()->setCurrentIndex(0);
        dialog.column_index_spin()->setValue(2);
        dialog.field_id_edit()->setText(QStringLiteral("0x0b400033"));
        dialog.title_edit()->setText(QStringLiteral("Text1"));
        dialog.accept();
        gate::check(dialog.ok(), "the table/date dialog accepted valid values");
        gate::check(dialog.edits().set_date_format &&
                        dialog.edits().date_format == 4,
                    "the result carries date format 4");
        gate::check(dialog.edits().replace_column && dialog.edits().column_index == 2 &&
                        dialog.edits().field_id == 0x0b400033u &&
                        dialog.edits().title == "Text1",
                    "the result carries the column replacement");
        check_button_box(dialog);
    }

    // -----------------------------------------------------------------------
    // Custom Field Formula
    // -----------------------------------------------------------------------
    {
        app::CustomFormulaEdit current;
        msqt::CustomFormulaDialog dialog(current);
        dialog.show();
        QApplication::processEvents();
        check_shell(dialog, "Custom Field Formula", kCustomFormulaSize);
        check_form(dialog, "formulaForm", 2);
        gate::check(dialog.field_combo() != nullptr && dialog.field_combo()->count() == 7,
                    "the formula field combo offers the seven measured custom fields");
        gate::check(dialog.formula_edit() != nullptr && dialog.formula_edit()->isVisible(),
                    "the formula text field is visible");
        std::vector<QPushButton*> buttons = {nullptr, dialog.clear_button(), nullptr};
        for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Set Formula")) {
                buttons[0] = button;
            } else if (button->text() == QStringLiteral("Cancel")) {
                buttons[2] = button;
            }
        }
        check_bottom_button_row(dialog, buttons);
        dialog.field_combo()->setCurrentIndex(0);
        dialog.formula_edit()->setText(QStringLiteral("[Duration]/240"));
        dialog.accept();
        gate::check(dialog.accepted_result(), "the formula dialog accepted a non-empty formula");
        gate::check(dialog.result().operation == app::CustomFormulaEdit::Operation::set &&
                        dialog.result().field_id != 0 &&
                        dialog.result().formula == "[Duration]/240",
                    "the result carries the set-formula request");
        check_vertical_hint(dialog, "formulaHint");
    }
    {
        app::CustomFormulaEdit current;
        msqt::CustomFormulaDialog dialog(current);
        dialog.show();
        QApplication::processEvents();
        dialog.clear_button()->click();
        gate::check(dialog.accepted_result() &&
                        dialog.result().operation == app::CustomFormulaEdit::Operation::clear,
                    "Clear Formulas returns the clear operation");
    }

    // -----------------------------------------------------------------------
    // Task Fields
    // -----------------------------------------------------------------------
    {
        app::TaskRow row;
        row.uid = 1;
        row.id = 1;
        row.name = "Design";
        core::sheet::SheetFormat format;
        msqt::TaskFieldsDialog dialog(row, format);
        dialog.show();
        QApplication::processEvents();
        check_shell(dialog, "Task Fields", kTaskFieldsSize);
        check_form(dialog, "taskFieldsForm", 6);
        gate::check(dialog.inputs().size() == 6, "Task Fields has the six value controls");
        check_vertical_hint(dialog, "taskFieldsHint");
        check_button_box(dialog);
        if (!dialog.inputs().empty()) {
            dialog.inputs()[0]->setText(QStringLiteral("hello"));
            dialog.accept();
            gate::check(dialog.edits().size() == 1,
                        "a changed Task Fields value produces one pending edit");
        }
    }

    // -----------------------------------------------------------------------
    // Predecessors
    // -----------------------------------------------------------------------
    {
        app::PlanInput loaded;
        app::TaskRow first;
        first.uid = 1;
        first.id = 1;
        first.name = "Design";
        app::TaskRow second;
        second.uid = 2;
        second.id = 2;
        second.name = "Build";
        loaded.tasks.push_back(first);
        loaded.tasks.push_back(second);
        app::StructuralEdits edits;
        msqt::PredecessorsDialog dialog(loaded, edits, 2);
        dialog.show();
        QApplication::processEvents();
        check_shell(dialog, "Predecessors", kPredecessorsSize);
        gate::check(dialog.list() != nullptr && dialog.list()->isVisible(),
                    "the predecessor list is visible");
        if (dialog.list() != nullptr) {
            gate::check(dialog.list()->width() == dialog.width() - 2 * kMargin,
                        "the predecessor list spans the content width");
            gate::check(dialog.list()->height() >= 140,
                        "the predecessor list keeps its 140 px minimum height");
        }
        check_button(dialog.delete_button(), "the Delete Selected button exists");
        gate::check(dialog.delete_button() != nullptr && !dialog.delete_button()->isEnabled(),
                    "Delete Selected starts disabled with no row selected");
        gate::check(dialog.predecessor_combo() != nullptr &&
                        dialog.predecessor_combo()->count() == 1,
                    "the predecessor combo lists the other task only");
        gate::check(dialog.type_combo() != nullptr && dialog.type_combo()->count() == 4,
                    "the type combo offers all four link types");
        gate::check(dialog.lag_edit() != nullptr && dialog.lag_edit()->width() == 70,
                    "the lag field is the pinned 70 px");
        check_button(dialog.add_button(), "the Add button exists");
        check_button_box(dialog);
        // The add path through app::make_link_add.
        if (dialog.predecessor_combo() != nullptr && dialog.predecessor_combo()->count() == 1) {
            dialog.predecessor_combo()->setCurrentIndex(0);
            dialog.type_combo()->setCurrentIndex(0);
            dialog.lag_edit()->setText(QStringLiteral("2"));
            dialog.add_button()->click();
            QApplication::processEvents();
            gate::check(dialog.pending_edits().size() == 1,
                        "Add queues one predecessor link edit");
            gate::check(dialog.list() != nullptr && dialog.list()->count() == 1,
                        "the added link appears in the list");
        }
    }

    // -----------------------------------------------------------------------
    // Filter
    // -----------------------------------------------------------------------
    {
        std::vector<app::FilterField> fields;
        fields.push_back(app::FilterField{0x0b40000eu, "Task Name", core::sheet::CellKind::Text});
        fields.push_back(app::FilterField{0x0b400507u, "Duration",
                                          core::sheet::CellKind::Duration});
        fields.push_back(app::FilterField{0x0b400010u, "Start", core::sheet::CellKind::Date});
        core::sheet::SheetFormat format;
        msqt::FilterDialog dialog(fields, format);
        dialog.show();
        QApplication::processEvents();
        check_shell(dialog, "Filter", kFilterSize);
        check_form(dialog, "filterForm", 3);
        gate::check(dialog.field_combo() != nullptr && dialog.field_combo()->count() == 3,
                    "the filter field combo lists the three columns");
        gate::check(dialog.operator_combo() != nullptr &&
                        dialog.operator_combo()->count() ==
                            static_cast<int>(app::filter_operators().size()),
                    "the operator combo lists every app operator");
        check_bottom_button_row(dialog, {dialog.apply_button(), dialog.clear_button(),
                                         dialog.cancel_button()});
        check_vertical_hint(dialog, "filterHint");
        dialog.field_combo()->setCurrentIndex(0);
        dialog.operator_combo()->setCurrentIndex(0);
        dialog.value_edit()->setText(QStringLiteral("Design"));
        dialog.apply_button()->click();
        gate::check(!dialog.cleared() && !dialog.description().empty(),
                    "Apply builds a filter definition and its description");
    }
    {
        std::vector<app::FilterField> fields;
        fields.push_back(app::FilterField{0x0b40000eu, "Task Name", core::sheet::CellKind::Text});
        core::sheet::SheetFormat format;
        msqt::FilterDialog dialog(fields, format);
        dialog.show();
        QApplication::processEvents();
        dialog.clear_button()->click();
        gate::check(dialog.cleared(), "Clear Filter reports the clear request");
    }

    // -----------------------------------------------------------------------
    // Preferences apply + persist, over the real window and the real sheet
    // -----------------------------------------------------------------------
    if (!fixture.isEmpty() && !out_dir.isEmpty()) {
        const QString ini = out_dir + QStringLiteral("/prefs-gate.ini");
        QFile::remove(ini);
        msqt::PreferencesStore store(ini);
        msqt::MainWindow window;
        window.set_preferences_store(&store);
        QString error;
        gate::check(window.open_file(fixture, &error), "the fixture opens for the prefs gate");
        window.show();
        QApplication::processEvents();

        const int duration_column = static_cast<int>(msqt::entry_column_index(
            msqt::EntryCellKind::duration));
        const int start_column =
            static_cast<int>(msqt::entry_column_index(msqt::EntryCellKind::start));
        gate::check(duration_column >= 0 && start_column >= 0,
                    "the Entry table has Duration and Start columns");
        // Read the DISPLAYED text (the Qt model's DisplayRole), not the core
        // sheet's own rendering: the duration style is a shell preference that
        // app::cell_text applies above the sheet (app/prefs.h).
        auto displayed = [&window](int column) {
            const QModelIndex index = window.sheet_table_model()->index(0, column);
            return window.sheet_table_model()
                ->data(index, Qt::DisplayRole)
                .toString()
                .toStdString();
        };
        const std::string before_duration =
            duration_column >= 0 ? displayed(duration_column) : std::string();

        app::Preferences chosen = window.preferences();
        chosen.date_style = app::DateStyle::iso;
        chosen.duration_style = app::DurationStyle::hours;
        chosen.zoom = app::ZoomLevel::day;
        chosen.working_day_shading = false;
        gate::check(window.apply_preferences(chosen, true), "Preferences [OK] applies");

        gate::check(window.zoom_slider() != nullptr &&
                        window.zoom_slider()->value() == static_cast<int>(app::ZoomLevel::day),
                    "the preference moved the zoom slider to Day");
        gate::check(window.gantt_view() != nullptr && !window.gantt_view()->shading(),
                    "the preference turned working-day shading off");
        if (window.sheet() != nullptr) {
            const std::string after_duration = displayed(duration_column);
            const std::string after_start = displayed(start_column);
            gate::check(after_duration.find("hrs") != std::string::npos,
                        "the hours duration preference changes Duration rendering");
            gate::check(after_start.find('-') != std::string::npos &&
                            after_start.find('/') == std::string::npos,
                        "the ISO date preference changes Start rendering");
            std::fprintf(stderr, "note: duration '%s' -> '%s', start '%s'\n",
                         before_duration.c_str(), after_duration.c_str(), after_start.c_str());
        }

        app::Preferences read_back;
        gate::check(store.load(read_back), "the preference line persists through QSettings");
        gate::check(read_back.date_style == app::DateStyle::iso &&
                        read_back.duration_style == app::DurationStyle::hours &&
                        read_back.zoom == app::ZoomLevel::day &&
                        !read_back.working_day_shading,
                    "the stored preference round-trips every changed field");
    }

    // -----------------------------------------------------------------------
    // Wave 23 (B-13, W23-19): Task Information is reachable through the ACTION
    // and through DOUBLE-CLICK, not only by constructing the dialog.  A
    // zero-delay timer finds the modal dialog, records its tabs/title and
    // rejects it, so the action's blocking exec() returns.
    // -----------------------------------------------------------------------
    if (!fixture.isEmpty()) {
        struct Seen {
            bool dialog = false;
            QString title;
            QString name;  // Task Information's Name field (which task)
            int tabs = 0;
        };
        const auto intercept = [](Seen* seen) {
            QTimer::singleShot(0, [seen]() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    auto* dialog = qobject_cast<QDialog*>(widget);
                    if (dialog == nullptr || !dialog->isVisible()) {
                        continue;
                    }
                    seen->dialog = true;
                    seen->title = dialog->windowTitle();
                    if (auto* tabs = dialog->findChild<QTabWidget*>(
                            QStringLiteral("taskInformationTabs"))) {
                        seen->tabs = tabs->count();
                    }
                    dialog->reject();
                    return;
                }
            });
        };

        msqt::MainWindow window;
        QString error;
        gate::check(window.open_file(fixture, &error), "the fixture opens for the action gate");
        window.show();
        QApplication::processEvents();

        QAction* information = gate::find_action(window, app::MenuAction::task_fields);
        gate::check(information != nullptr, "Task > Information is a command");
        if (information != nullptr) {
            Seen seen;
            intercept(&seen);
            information->trigger();
            QApplication::processEvents();
            gate::check(seen.dialog && seen.title == QStringLiteral("Task Information"),
                        "the Information ACTION opens Task Information");
            gate::check(seen.tabs == 6, "the dialog has Project's six tabs");
        }

        QAction* custom = gate::find_action(window, app::MenuAction::custom_fields);
        gate::check(custom != nullptr, "Custom Fields is its own command (Q10)");
        if (custom != nullptr) {
            Seen seen;
            intercept(&seen);
            custom->trigger();
            QApplication::processEvents();
            gate::check(seen.dialog && seen.title == QStringLiteral("Custom Fields"),
                        "the Custom Fields command opens the field-definition dialog");
        }

        // Wave 24 (W24-6): a REAL double-click through QTest::mouseDClick on
        // the sheet viewport.  Wave 23 emitted the doubleClicked signal
        // programmatically, which proved nothing about the mouse path.  This
        // fixture hides the project row, so row 0 is the first task.
        QTableView* sheet = window.sheet_view();
        if (sheet != nullptr && sheet->model() != nullptr) {
            const int name_column =
                window.sheet_table_model()->display_column_for_field(app::name_field_id());
            if (name_column >= 0 && sheet->model()->rowCount() > 0) {
                const QModelIndex index = sheet->model()->index(0, name_column);
                sheet->scrollTo(index);
                QApplication::processEvents();
                Seen seen;
                QTimer timer;
                timer.setInterval(10);
                QObject::connect(&timer, &QTimer::timeout, [&seen, &timer]() {
                    for (QWidget* widget : QApplication::topLevelWidgets()) {
                        auto* dialog = qobject_cast<QDialog*>(widget);
                        if (dialog == nullptr || !dialog->isVisible()) {
                            continue;
                        }
                        seen.dialog = true;
                        seen.title = dialog->windowTitle();
                        if (auto* info = dynamic_cast<msqt::TaskInformationDialog*>(dialog)) {
                            if (info->name_edit() != nullptr) {
                                seen.name = info->name_edit()->text();
                            }
                        }
                        if (auto* tabs = dialog->findChild<QTabWidget*>(
                                QStringLiteral("taskInformationTabs"))) {
                            seen.tabs = tabs->count();
                        }
                        dialog->reject();
                        timer.stop();
                        return;
                    }
                });
                timer.start();
                gate::check(sheet->indexAt(sheet->visualRect(index).center()) == index,
                            "the double-click target resolves to the task cell");
                int double_clicks = 0;
                QObject::connect(sheet, &QAbstractItemView::doubleClicked,
                                 [&double_clicks](const QModelIndex&) { ++double_clicks; });
                // QTest::mouseDClick sends only the DblClick event; a real
                // sequence also presses first, which is what sets the view's
                // pressedIndex.  Send press -> dblclick -> release.
                const QPoint target = sheet->visualRect(index).center();
                QTest::mousePress(sheet->viewport(), Qt::LeftButton, Qt::NoModifier, target);
                QTest::mouseDClick(sheet->viewport(), Qt::LeftButton, Qt::NoModifier, target);
                QTest::mouseRelease(sheet->viewport(), Qt::LeftButton, Qt::NoModifier, target);
                QApplication::processEvents();
                timer.stop();
                gate::check(seen.dialog && seen.title == QStringLiteral("Task Information"),
                            "a real double-click opens Task Information");
                gate::check(double_clicks == 1,
                            "the view emitted doubleClicked exactly once");
                // Second-agent review: the title alone does not prove WHICH
                // task the dialog describes.  synthetic-base's row 0 is ID 1.
                gate::check(seen.name == QStringLiteral("Task A"),
                            "the dialog describes the clicked task (ID 1)");
            } else {
                gate::check(false, "the sheet exposes a Name column and a task row");
            }
        } else {
            gate::check(false, "the sheet view has a model for the double-click gate");
        }
    }

    // -----------------------------------------------------------------------
    // Wave 24 (W24-6/R5): a real double-click on the PROJECT row must not open
    // an editable Task Information dialog, because its edits are dropped by
    // sync_plan_from_records.  Uses a second fixture whose file stores the
    // project row shown.
    // -----------------------------------------------------------------------
    if (!project_fixture.isEmpty()) {
        struct Seen {
            bool dialog = false;
            QString title;
        };
        msqt::MainWindow window;
        QString error;
        gate::check(window.open_file(project_fixture, &error),
                    "the project-row fixture opens");
        window.show();
        QApplication::processEvents();
        QTableView* sheet = window.sheet_view();
        gate::check(sheet != nullptr && sheet->model() != nullptr &&
                        sheet->model()->rowCount() > 1 &&
                        window.sheet()->row_uid(0) == 0,
                    "the project-row fixture shows the project row first");
        if (sheet != nullptr && sheet->model() != nullptr && sheet->model()->rowCount() > 1) {
            const int name_column =
                window.sheet_table_model()->display_column_for_field(app::name_field_id());
            const QModelIndex project_index = sheet->model()->index(0, name_column);
            sheet->scrollTo(project_index);
            QApplication::processEvents();
            Seen seen;
            // A polling timer (not singleShot(0)) because QTest::mouseDClick
            // processes events between the two clicks, which could otherwise
            // consume the timer before the dialog's nested loop starts.
            QTimer timer;
            timer.setInterval(10);
            QObject::connect(&timer, &QTimer::timeout, [&seen, &timer]() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    auto* dialog = qobject_cast<QDialog*>(widget);
                    if (dialog == nullptr || !dialog->isVisible()) {
                        continue;
                    }
                    seen.dialog = true;
                    seen.title = dialog->windowTitle();
                    dialog->reject();
                    timer.stop();
                    return;
                }
            });
            timer.start();
            const QPoint project_target = sheet->visualRect(project_index).center();
            gate::check(sheet->indexAt(project_target) == project_index,
                        "the project-row double-click lands on the project row");
            int project_clicks = 0;
            QObject::connect(sheet, &QAbstractItemView::doubleClicked,
                             [&project_clicks](const QModelIndex&) { ++project_clicks; });
            QTest::mousePress(sheet->viewport(), Qt::LeftButton, Qt::NoModifier,
                              project_target);
            QTest::mouseDClick(sheet->viewport(), Qt::LeftButton, Qt::NoModifier,
                               project_target);
            QTest::mouseRelease(sheet->viewport(), Qt::LeftButton, Qt::NoModifier,
                                project_target);
            QApplication::processEvents();
            timer.stop();
            gate::check(project_clicks == 1,
                        "the project-row double-click reached the view (signal emitted)");
            gate::check(!seen.dialog,
                        "a double-click on the project row opens no editable dialog");
            const QModelIndex task_index = sheet->model()->index(1, name_column);
            Seen task_seen;
            QTimer task_timer;
            task_timer.setInterval(10);
            QObject::connect(&task_timer, &QTimer::timeout, [&task_seen, &task_timer]() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    auto* dialog = qobject_cast<QDialog*>(widget);
                    if (dialog == nullptr || !dialog->isVisible()) {
                        continue;
                    }
                    task_seen.dialog = true;
                    task_seen.title = dialog->windowTitle();
                    dialog->reject();
                    task_timer.stop();
                    return;
                }
            });
            task_timer.start();
            gate::check(sheet->indexAt(sheet->visualRect(task_index).center()) == task_index,
                        "the task double-click target resolves to its cell");
            const QPoint task_target = sheet->visualRect(task_index).center();
            QTest::mousePress(sheet->viewport(), Qt::LeftButton, Qt::NoModifier, task_target);
            QTest::mouseDClick(sheet->viewport(), Qt::LeftButton, Qt::NoModifier, task_target);
            QTest::mouseRelease(sheet->viewport(), Qt::LeftButton, Qt::NoModifier,
                                task_target);
            QApplication::processEvents();
            task_timer.stop();
            gate::check(task_seen.dialog &&
                            task_seen.title == QStringLiteral("Task Information"),
                        "the task below the project row still opens Task Information");
        }
    }

    std::printf("qt_dialog_gate: ");
    return gate::finish();
}
