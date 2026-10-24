// app_qt/tests/project_dialog_gate_test.cpp -- wave 26 G5's structural gate for
// the Project tab's dialogs.
//
// The measured source is the local, git-ignored audit capture set.  The numbers
// the gate compares against are committed in
// tests/data/reference/w26-project-dialogs.measures.json (fractions of each
// captured dialog's client box, plus the captured control inventory).  Nothing
// here reads the port's own constants for a measured value: the JSON is parsed,
// and the LIVE widget tree is measured.
//
// Each dialog is constructed and SHOWN offscreen without exec(), then its size,
// measured layout fractions, control kinds, labels, enabled states and exact
// disabled tooltips are checked.  The functional paths are driven directly.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "gate_util.h"
#include "main_window.h"  // configure_application()
#include "project_dialogs.h"
#include "project_dialogs_qt.h"

namespace {

using msqt::project_dialog_geometry::kNotImplemented;

QJsonObject load_measures(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

bool close_enough(double got, double want, double tolerance) {
    return std::fabs(got - want) <= tolerance;
}

void check_fraction(double actual_px, int total_px, double want, double tolerance,
                    const char* what) {
    gate::check(total_px > 0, "the dialog has a positive measured dimension");
    if (total_px <= 0) {
        return;
    }
    const double got = actual_px / total_px;
    gate::check(close_enough(got, want, tolerance), what);
    if (!close_enough(got, want, tolerance)) {
        std::fprintf(stderr, "  fraction %.4f, measured %.4f (tol %.3f)\n", got, want,
                     tolerance);
    }
}

// ---------------------------------------------------------------------------
// Project Information
// ---------------------------------------------------------------------------
void check_project_information(const QJsonObject& measures) {
    const QJsonObject spec = measures.value(QStringLiteral("project_information")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Project Information");

    app::ProjectInformation info;
    info.title = "Synthetic";
    info.start_text = "Sun 04/01/26";
    info.finish_text = "Fri 09/01/26";
    info.current_text = "Fri 23/10/26";
    info.start_day = core::sched::from_ymd(2026, 1, 4);
    info.finish_day = core::sched::from_ymd(2026, 1, 9);
    info.current_day = core::sched::from_ymd(2026, 10, 23);
    info.has_start = info.has_finish = true;
    info.calendars = {"Standard", "Shift A"};
    info.calendar = "Standard";
    info.has_calendar = true;
    info.priority = 500;

    msqt::ProjectInformationDialog dialog(info);
    dialog.show();
    QApplication::processEvents();

    const int want_width = spec.value(QStringLiteral("width")).toInt();
    const int want_height = spec.value(QStringLiteral("height")).toInt();
    gate::check(dialog.width() == want_width && dialog.height() == want_height,
                "Project Information has the measured fixed size");
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "Project Information is non-resizable");
    check_fraction(dialog.height(), dialog.width(),
                   spec.value(QStringLiteral("aspect")).toDouble(), 0.03,
                   "Project Information has the captured aspect ratio");
    gate::check(dialog.windowTitle() == QStringLiteral("Project Information for 'Synthetic'"),
                "the title is the capture's 'Project Information for <project>'");

    // The captured labels, in order.
    const QJsonArray labels = spec.value(QStringLiteral("labels")).toArray();
    QStringList live_labels;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        if (!label->text().isEmpty()) {
            live_labels.push_back(label->text());
        }
    }
    for (const QJsonValue& value : labels) {
        gate::check(live_labels.contains(value.toString()),
                    "the captured label exists on the live dialog");
    }

    // Horizontal columns and the first three captured row positions.
    const double label_fraction = spec.value(QStringLiteral("label_left_fraction")).toDouble();
    const double field_fraction = spec.value(QStringLiteral("field_left_fraction")).toDouble();
    QLabel* start_label = nullptr;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Start date:")) {
            start_label = label;
        }
    }
    gate::check(start_label != nullptr, "the Start date label is a live widget");
    if (start_label != nullptr) {
        check_fraction(start_label->x(), dialog.width(), label_fraction, 0.03,
                       "the form labels start at the capture's left fraction");
    }
    check_fraction(dialog.start_edit()->x(), dialog.width(), field_fraction, 0.03,
                   "the left fields start at the capture's field fraction");

    const QJsonArray rows = spec.value(QStringLiteral("row_top_fractions")).toArray();
    QWidget* row_widgets[] = {dialog.start_edit(), dialog.finish_edit(),
                              dialog.schedule_from_combo()};
    for (int row = 0; row < 3 && row < rows.size(); ++row) {
        check_fraction(row_widgets[row]->y(), dialog.height(), rows.at(row).toDouble(), 0.03,
                       "the captured field row sits at its measured top fraction");
    }
    check_fraction(dialog.ok_button()->y(), dialog.height(),
                   spec.value(QStringLiteral("button_row_top_fraction")).toDouble(), 0.02,
                   "the button row sits on the capture's bottom band");

    // Control kinds and captured state.
    gate::check(qobject_cast<QDateEdit*>(dialog.start_edit()) != nullptr &&
                    qobject_cast<QDateEdit*>(dialog.finish_edit()) != nullptr &&
                    qobject_cast<QDateEdit*>(dialog.current_edit()) != nullptr &&
                    qobject_cast<QDateEdit*>(dialog.status_edit()) != nullptr,
                "the four captured date fields are QDateEdits");
    gate::check(dialog.schedule_from_combo()->count() == 2 &&
                    dialog.schedule_from_combo()->currentText() ==
                        QStringLiteral("Project Start Date"),
                "Schedule from offers Project's two choices, initially Project Start Date");
    gate::check(dialog.calendar_combo()->count() == 2 &&
                    dialog.calendar_combo()->currentText() == QStringLiteral("Standard"),
                "the Calendar combo carries the file's base calendars and default");
    const QJsonArray range = spec.value(QStringLiteral("spin_range")).toArray();
    gate::check(dialog.priority_spin()->minimum() == range.at(0).toInt() &&
                    dialog.priority_spin()->maximum() == range.at(1).toInt(),
                "Priority is a 0..1000 spin box");
    gate::check(dialog.priority_spin()->value() == 500 && dialog.priority_spin()->isEnabled(),
                "Priority shows the file's 500 and is the one enabled edit");
    gate::check(dialog.enterprise_grid()->columnCount() == 2 &&
                    dialog.enterprise_grid()->rowCount() == 0,
                "the Enterprise Custom Fields grid is the captured two-column empty grid");

    // Decision 9: every control without an engine is disabled with the exact
    // tooltip, never silently enabled.
    const QString tooltip = QString::fromUtf8(kNotImplemented);
    gate::check(tooltip == spec.value(QStringLiteral("disabled_tooltip")).toString(),
                "the disabled tooltip is the measures file's exact string");
    QWidget* disabled[] = {dialog.start_edit(),      dialog.finish_edit(),
                           dialog.current_edit(),    dialog.status_edit(),
                           dialog.schedule_from_combo(), dialog.calendar_combo(),
                           dialog.enterprise_grid(), dialog.help_button(),
                           dialog.statistics_button()};
    for (QWidget* control : disabled) {
        gate::check(control != nullptr && !control->isEnabled(),
                    "a control with no engine is disabled");
        if (control != nullptr) {
            gate::check(control->toolTip() == tooltip,
                        "a disabled control carries the exact decision-9 tooltip");
        }
    }

    // The one functional field: OK returns the chosen priority.
    dialog.priority_spin()->setValue(750);
    gate::check(dialog.priority_changed(), "changing Priority marks the dialog changed");
    dialog.accept();
    gate::check(dialog.result() == QDialog::Accepted, "OK accepts the dialog");
    gate::check(dialog.priority() == 750, "OK returns the chosen priority");
    std::fprintf(stderr, "note: Project Information %dx%d, rows %.3f/%.3f/%.3f\n",
                 dialog.width(), dialog.height(), dialog.start_edit()->y() / double(dialog.height()),
                 dialog.finish_edit()->y() / double(dialog.height()),
                 dialog.schedule_from_combo()->y() / double(dialog.height()));

    // An unchanged priority is reported as unchanged.
    msqt::ProjectInformationDialog unchanged(info);
    unchanged.show();
    QApplication::processEvents();
    unchanged.accept();
    gate::check(!unchanged.priority_changed(), "an untouched Priority is not a change");
    gate::check(unchanged.priority() == 500, "an untouched Priority keeps the file's value");
}

// ---------------------------------------------------------------------------
// Change Working Time
// ---------------------------------------------------------------------------
QColor rendered_pixel(QWidget* widget, int x, int y) {
    QImage image(widget->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    widget->render(&image);
    return image.pixelColor(x, y);
}

bool colour_close(const QColor& got, const QString& want_hex, int tolerance) {
    const QColor want(want_hex);
    return want.isValid() && std::abs(got.red() - want.red()) <= tolerance &&
           std::abs(got.green() - want.green()) <= tolerance &&
           std::abs(got.blue() - want.blue()) <= tolerance;
}

void check_change_working_time(const QJsonObject& measures, const QString& data_dir) {
    const QJsonObject spec =
        measures.value(QStringLiteral("change_working_time")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Change Working Time");
    const QString fixture = data_dir + QStringLiteral("/synthetic-calendars.mpp");

    msqt::ChangeWorkingTimeDialog dialog(fixture.toStdString(), core::sheet::SheetFormat{},
                                         2026, 10, 23);
    dialog.show();
    QApplication::processEvents();

    gate::check(dialog.width() == spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() == spec.value(QStringLiteral("height")).toInt(),
                "Change Working Time has the measured fixed size");
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "Change Working Time is non-resizable");
    const double aspect = double(dialog.width()) / dialog.height();
    gate::check(close_enough(aspect, spec.value(QStringLiteral("aspect")).toDouble(), 0.03),
                "Change Working Time has the captured aspect ratio");

    const app::WorkingTimeModel& model = dialog.model();
    gate::check(model.decoded, "the dialog decoded the fixture's calendar table");
    gate::check(model.calendars.size() == 2 && model.selected_calendar == "Standard",
                "the picker lists the file's base calendars and selects the default");
    gate::check(dialog.calendar_combo()->count() == 2 &&
                    dialog.calendar_combo()->currentText() == QStringLiteral("Standard"),
                "the live picker is the file's list with Standard selected");
    gate::check(dialog.calendar_line()->text() ==
                    QStringLiteral("Calendar 'Standard' is a base calendar."),
                "the capture's 'is a base calendar' line names the file's calendar");
    gate::check(dialog.month_label()->text() == QStringLiteral("October 2026"),
                "the month header is the requested month");
    gate::check(dialog.day_cells().size() == 31u,
                "the live month grid has one cell per October day");
    gate::check(dialog.hours_label()->text() ==
                    QStringLiteral("8:00 AM to 12:00 PM\n1:00 PM to 5:00 PM"),
                "the right pane shows the selected day's captured intervals");
    gate::check(dialog.based_on_label()->text().contains(
                    QStringLiteral("Default work week on calendar Standard")),
                "the Based on line names the decoded default work week");

    // The weekday header is Project's Monday-first order, read from the live
    // labels above the cells.
    QStringList weekday_labels;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text().size() <= 2 && label->font().bold() &&
            label->parentWidget() == dialog.findChild<QWidget*>(QStringLiteral("monthHost"))) {
            weekday_labels.push_back(label->text());
        }
    }
    const QJsonArray header = spec.value(QStringLiteral("weekday_header")).toArray();
    gate::check(weekday_labels.size() == header.size(),
                "the live grid has the seven captured weekday headers");
    for (int i = 0; i < header.size() && i < weekday_labels.size(); ++i) {
        gate::check(weekday_labels.at(i) == header.at(i).toString(),
                    "the weekday header is the capture's M T W Th F S S");
    }

    // The legend: five live swatches whose rendered fills are the captured
    // colours.
    const QJsonArray legend = spec.value(QStringLiteral("legend_colours")).toArray();
    gate::check(dialog.legend_swatches().size() == static_cast<std::size_t>(legend.size()),
                "the legend has the five captured entries");
    for (int i = 0; i < legend.size() && i < int(dialog.legend_swatches().size()); ++i) {
        QWidget* swatch = dialog.legend_swatches()[static_cast<std::size_t>(i)];
        gate::check(swatch != nullptr && swatch->width() ==
                                              msqt::project_dialog_geometry::kLegendSwatch,
                    "a legend swatch is the measured square");
        if (swatch != nullptr) {
            gate::check(colour_close(rendered_pixel(swatch, 2, 2), legend.at(i).toString(), 12),
                        "the legend swatch renders the captured fill colour");
        }
    }

    // The month grid's cells: day 3 is a Saturday (non-working, grey), day 23
    // is the selected working Friday (black).  Both fills are read from the
    // RENDERED live widget.
    const QJsonObject fills = spec.value(QStringLiteral("day_fills")).toObject();
    gate::check(dialog.day_cells().size() > 22, "the October grid reaches day 23");
    if (dialog.day_cells().size() > 22) {
        QWidget* saturday = dialog.day_cells()[2];
        QWidget* friday = dialog.day_cells()[22];
        gate::check(saturday != nullptr && !saturday->property("working").toBool(),
                    "day 3 is a non-working live cell");
        gate::check(saturday != nullptr &&
                        colour_close(rendered_pixel(saturday, 2, 2),
                                     fills.value(QStringLiteral("nonworking")).toString(), 12),
                    "the weekend cell renders the captured non-working grey");
        gate::check(friday != nullptr && friday->property("selected").toBool() &&
                        friday->property("working").toBool(),
                    "day 23 is the selected working live cell");
        gate::check(friday != nullptr &&
                        colour_close(rendered_pixel(friday, 2, 2),
                                     fills.value(QStringLiteral("selected")).toString(), 12),
                    "the selected day renders the captured black fill");
    }

    // Picking the second calendar reloads the same month and shows its week.
    dialog.calendar_combo()->setCurrentIndex(1);
    QApplication::processEvents();
    gate::check(dialog.model().selected_calendar == "Shift A",
                "the live picker switches to the second base calendar");
    gate::check(dialog.hours_label()->text() ==
                    QStringLiteral("9:00 AM to 1:00 PM\n2:00 PM to 6:00 PM"),
                "Shift A's own captured week is shown");
    dialog.select_day(23);
    QApplication::processEvents();
    gate::check(dialog.selected_day() == 23 &&
                    dialog.day_cells().size() > 22 &&
                    dialog.day_cells()[22]->property("selected").toBool(),
                "clicking a day moves the selection in the live grid");

    // The captured tables start empty on this fixture (no exceptions or work
    // weeks), with the capture's columns.
    gate::check(dialog.tabs()->count() == 2 &&
                    dialog.tabs()->tabText(0) == QStringLiteral("Exceptions") &&
                    dialog.tabs()->tabText(1) == QStringLiteral("Work Weeks"),
                "the dialog has the capture's two tabs");
    gate::check(dialog.exceptions_table()->columnCount() == 4 &&
                    dialog.exceptions_table()->horizontalHeaderItem(2)->text() ==
                        QStringLiteral("Start") &&
                    dialog.exceptions_table()->horizontalHeaderItem(3)->text() ==
                        QStringLiteral("Finish"),
                "the tables carry the capture's Start/Finish columns");
    gate::check(dialog.exceptions_table()->rowCount() == 0 &&
                    dialog.work_weeks_table()->rowCount() == 0,
                "the synthetic fixture stores no exceptions or work weeks");

    const QString tooltip = QString::fromUtf8(
        msqt::project_dialog_geometry::kNotImplemented);
    QWidget* disabled[] = {dialog.details_button(), dialog.delete_button(),
                           dialog.options_button()};
    for (QWidget* control : disabled) {
        gate::check(control != nullptr && !control->isEnabled() &&
                        control->toolTip() == tooltip,
                    "a calendar-edit control is disabled with the exact decision-9 tooltip");
    }

    // Month navigation rebuilds the live grid.
    dialog.set_month(2026, 11);
    QApplication::processEvents();
    gate::check(dialog.month_label()->text() == QStringLiteral("November 2026") &&
                    dialog.day_cells().size() == 30u,
                "changing the month rebuilds the live day grid");
    std::fprintf(stderr, "note: Change Working Time %dx%d, %zu day cells, %zu legend swatches\n",
                 dialog.width(), dialog.height(), dialog.day_cells().size(),
                 dialog.legend_swatches().size());
}

// ---------------------------------------------------------------------------
// Custom Fields (the field-definition dialog)
// ---------------------------------------------------------------------------
void check_custom_fields(const QJsonObject& measures) {
    const QJsonObject spec = measures.value(QStringLiteral("custom_fields")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Custom Fields");
    const std::vector<app::CustomFieldDefinition> definitions =
        app::custom_field_definitions();
    gate::check(definitions.size() == 100,
                "the definition list is the six measured families' every field");

    msqt::CustomFieldsDialog dialog(definitions);
    dialog.show();
    QApplication::processEvents();

    gate::check(dialog.width() == spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() == spec.value(QStringLiteral("height")).toInt(),
                "Custom Fields has the measured fixed size");
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "Custom Fields is non-resizable");
    const double aspect = double(dialog.width()) / dialog.height();
    gate::check(close_enough(aspect, spec.value(QStringLiteral("aspect")).toDouble(), 0.03),
                "Custom Fields has the captured aspect ratio");

    gate::check(dialog.field_list()->count() == 100,
                "the live field list carries every definition");
    int text1_row = -1;
    for (int row = 0; row < dialog.field_list()->count(); ++row) {
        if (dialog.field_list()->item(row)->text() == QStringLiteral("Text1")) {
            text1_row = row;
        }
    }
    gate::check(text1_row >= 0, "Text1 is in the live list (the capture's family order)");
    gate::check(dialog.field_list()->item(0)->text() == QStringLiteral("Duration1") &&
                    dialog.field_list()->item(99)->text() == QStringLiteral("Text30"),
                "the list runs Duration1..Text30 in the measured family order");

    const QString tooltip = QString::fromUtf8(
        msqt::project_dialog_geometry::kNotImplemented);
    gate::check(dialog.task_radio()->isChecked() && dialog.task_radio()->isEnabled(),
                "the capture's Task type is selected and live");
    QRadioButton* scopes[] = {dialog.resource_radio(), dialog.project_radio()};
    for (QRadioButton* scope : scopes) {
        gate::check(scope != nullptr && !scope->isEnabled() && scope->toolTip() == tooltip,
                    "an unimplemented field scope is disabled with the exact tooltip");
    }
    QPushButton* definition_buttons[] = {dialog.rename_button(), dialog.delete_button(),
                                         dialog.add_field_button(),
                                         dialog.import_field_button(), dialog.help_button()};
    for (QPushButton* button : definition_buttons) {
        gate::check(button != nullptr && !button->isEnabled() &&
                        button->toolTip() == tooltip,
                    "a definition action with no writer is disabled with the exact tooltip");
    }

    // The selected field's type is DISPLAYED from the field registry, not guessed.
    dialog.field_list()->setCurrentRow(0);
    QApplication::processEvents();
    gate::check(dialog.type_combo()->currentText() == QStringLiteral("Duration"),
                "the Type combo shows the selected field's own kind");
    gate::check(!dialog.type_combo()->isEnabled() &&
                    dialog.type_combo()->toolTip() == tooltip,
                "changing a field's type is disabled with the exact tooltip");
    if (text1_row >= 0) {
        dialog.field_list()->setCurrentRow(text1_row);
        QApplication::processEvents();
        gate::check(dialog.type_combo()->currentText() == QStringLiteral("Text"),
                    "selecting Text1 shows the Text kind");
    }

    // The Formula... path: the editor's returned text becomes the pending
    // app::CustomFormulaEdit for THIS field.
    bool asked = false;
    dialog.set_formula_editor([&asked](std::uint32_t, const QString&) -> QString {
        asked = true;
        return QStringLiteral("[Duration]/240");
    });
    gate::check(dialog.formula_button()->isEnabled(),
                "Formula... is the one enabled definition action");
    dialog.formula_button()->click();
    QApplication::processEvents();
    gate::check(asked, "Formula... asks the installed editor");
    gate::check(dialog.has_formula_edit(),
                "the formula result becomes a pending edit");
    if (text1_row >= 0) {
        gate::check(dialog.formula_edit().operation ==
                        app::CustomFormulaEdit::Operation::set &&
                        dialog.formula_edit().field_id ==
                            definitions[static_cast<std::size_t>(text1_row)].field_id &&
                        dialog.formula_edit().formula == "[Duration]/240",
                    "the pending edit targets the selected field with the typed text");
    }
    dialog.accept();
    gate::check(dialog.result() == QDialog::Accepted, "OK accepts the definition dialog");

    // A cancelled editor queues nothing.
    msqt::CustomFieldsDialog cancelled(definitions);
    cancelled.set_formula_editor([](std::uint32_t, const QString&) -> QString {
        return QString();  // null = cancel
    });
    cancelled.show();
    QApplication::processEvents();
    cancelled.formula_button()->click();
    QApplication::processEvents();
    gate::check(!cancelled.has_formula_edit(),
                "a cancelled formula editor queues no edit");
    std::fprintf(stderr, "note: Custom Fields %dx%d, %d definitions\n", dialog.width(),
                 dialog.height(), static_cast<int>(dialog.field_list()->count()));
}

// ---------------------------------------------------------------------------
// Update Project
// ---------------------------------------------------------------------------
void check_update_project(const QJsonObject& measures) {
    const QJsonObject spec = measures.value(QStringLiteral("update_project")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Update Project");

    msqt::UpdateProjectDialog dialog;
    dialog.show();
    QApplication::processEvents();
    gate::check(dialog.width() == spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() == spec.value(QStringLiteral("height")).toInt(),
                "Update Project has the measured fixed size");
    const double aspect = double(dialog.width()) / dialog.height();
    gate::check(close_enough(aspect, spec.value(QStringLiteral("aspect")).toDouble(), 0.03),
                "Update Project has the captured aspect ratio");

    QStringList live_texts;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        live_texts.push_back(label->text());
    }
    for (QRadioButton* radio : dialog.findChildren<QRadioButton*>()) {
        live_texts.push_back(radio->text());
    }
    const QJsonArray captions = spec.value(QStringLiteral("captions")).toArray();
    for (const QJsonValue& value : captions) {
        gate::check(live_texts.contains(value.toString()),
                    "the captured Update Project caption exists on the live dialog");
    }
    gate::check(dialog.radios().size() == 5,
                "the live dialog has the capture's five choices");
    gate::check(dialog.through_date() != nullptr && dialog.after_date() != nullptr,
                "both captured date fields are live widgets");
    const QString tooltip = QString::fromUtf8(
        msqt::project_dialog_geometry::kNotImplemented);
    gate::check(!dialog.ok_button()->isEnabled() &&
                    dialog.ok_button()->toolTip() == tooltip,
                "OK is disabled with the exact decision-9 tooltip (no actuals writer)");
    gate::check(dialog.cancel_button()->isEnabled(), "Cancel works");
    std::fprintf(stderr, "note: Update Project %dx%d, %d choices\n", dialog.width(),
                 dialog.height(), static_cast<int>(dialog.radios().size()));
}

// ---------------------------------------------------------------------------
// Links Between Projects
// ---------------------------------------------------------------------------
void check_links_between_projects(const QJsonObject& measures) {
    const QJsonObject spec =
        measures.value(QStringLiteral("links_between_projects")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Links Between Projects");
    msqt::LinksBetweenProjectsDialog dialog("C:/w26/synthetic.mpp");
    dialog.show();
    QApplication::processEvents();
    gate::check(dialog.width() == spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() == spec.value(QStringLiteral("height")).toInt(),
                "Links Between Projects has the measured fixed size");
    const QJsonArray tabs = spec.value(QStringLiteral("tabs")).toArray();
    gate::check(dialog.tabs()->count() == tabs.size(),
                "the live dialog has the capture's two external-link tabs");
    for (int index = 0; index < tabs.size() && index < dialog.tabs()->count(); ++index) {
        gate::check(dialog.tabs()->tabText(index) == tabs.at(index).toString(),
                    "the external-link tab is the capture's label in order");
    }
    const QJsonArray external_columns =
        spec.value(QStringLiteral("external_columns")).toArray();
    gate::check(dialog.external_table()->columnCount() == external_columns.size(),
                "the external table has the capture's five columns");
    for (int column = 0; column < external_columns.size() &&
                         column < dialog.external_table()->columnCount();
         ++column) {
        gate::check(dialog.external_table()->horizontalHeaderItem(column)->text() ==
                        external_columns.at(column).toString(),
                    "an external table column is the capture's");
    }
    const QJsonArray links_columns = spec.value(QStringLiteral("links_columns")).toArray();
    gate::check(dialog.links_table()->columnCount() == links_columns.size(),
                "the Links table has the capture's four columns");
    for (int column = 0; column < links_columns.size() &&
                         column < dialog.links_table()->columnCount();
         ++column) {
        gate::check(dialog.links_table()->horizontalHeaderItem(column)->text() ==
                        links_columns.at(column).toString(),
                    "a Links column is the capture's");
    }
    const QString tooltip = QString::fromUtf8(
        msqt::project_dialog_geometry::kNotImplemented);
    QWidget* disabled[] = {dialog.help_button(), dialog.show_button(), dialog.open_button(),
                           dialog.refresh_button(), dialog.details_button(),
                           dialog.options_button(), dialog.path_edit()};
    for (QWidget* control : disabled) {
        gate::check(control != nullptr && !control->isEnabled() &&
                        control->toolTip() == tooltip,
                    "an external-link action is disabled with the exact tooltip");
    }
    gate::check(dialog.close_button()->isEnabled(), "Close is the live exit");
    gate::check(dialog.windowTitle().startsWith(
                    QStringLiteral("Links Between Projects in ")),
                "the title carries the project path, as the capture does");
}

// ---------------------------------------------------------------------------
// The WBS and Set Baseline drop-down menus (live ribbon controls)
// ---------------------------------------------------------------------------
QWidget* ribbon_control_by_label(msqt::MainWindow& window, const QString& tab,
                                 const QString& group, const QString& label) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    ribbon->set_active_tab(ribbon->tab_titles().indexOf(tab));
    QApplication::processEvents();
    const int group_index = ribbon->group_titles().indexOf(group);
    if (group_index < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group_index); ++control) {
        QWidget* live = ribbon->group_control(group_index, control);
        if (live == nullptr) {
            continue;
        }
        QString text = live->accessibleName();
        if (auto* button = qobject_cast<QToolButton*>(live)) {
            text = button->text().isEmpty() ? button->accessibleName() : button->text();
        }
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (text.simplified() == label) {
            return live;
        }
    }
    return nullptr;
}

void check_ribbon_menus(const QJsonObject& measures, const QString& data_dir) {
    msqt::MainWindow window;
    QString error;
    gate::check(window.open_file(data_dir + QStringLiteral("/synthetic-base.mpp"), &error),
                "the fixture opens for the menu gate");
    window.show();
    QApplication::processEvents();

    const struct {
        const char* name;
        const char* group;
        const char* label;
        const char* items_key;
    } kMenus[] = {
        {"WBS", "Properties", "WBS", "wbs_menu"},
        {"Set Baseline", "Schedule", "Set Baseline", "baseline_menu"},
    };
    for (const auto& menu : kMenus) {
        QWidget* control = ribbon_control_by_label(window, QStringLiteral("Project"),
                                                   QString::fromUtf8(menu.group),
                                                   QString::fromUtf8(menu.label));
        gate::check(control != nullptr, "the captured drop-down control is live");
        auto* button = qobject_cast<QToolButton*>(control);
        gate::check(button != nullptr && button->menu() != nullptr,
                    "the control owns a QMenu");
        if (button == nullptr || button->menu() == nullptr) {
            continue;
        }
        gate::check(button->isEnabled(), "the drop-down control can be opened");
        gate::check(msqt::RibbonBar::control_kind(button) == QStringLiteral("split"),
                    "the live control is the split-button class the capture shows");
        const QJsonObject spec = measures.value(QString::fromUtf8(menu.items_key)).toObject();
        const QJsonArray items = spec.value(QStringLiteral("items")).toArray();
        const QList<QAction*> actions = button->menu()->actions();
        gate::check(actions.size() == items.size(),
                    "the menu has the capture's items");
        for (int index = 0; index < items.size() && index < actions.size(); ++index) {
            gate::check(actions.at(index)->text() == items.at(index).toString(),
                        "the menu item is the capture's label in order");
            gate::check(!actions.at(index)->isEnabled() &&
                            actions.at(index)->toolTip() ==
                                QString::fromUtf8(
                                    msqt::project_dialog_geometry::kNotImplemented),
                        "a menu item with no engine is disabled with the exact tooltip");
        }
    }
}

// ---------------------------------------------------------------------------
// Move Project's actual-values prompt
// ---------------------------------------------------------------------------
struct PromptSeen {
    bool seen = false;
    QString text;
    bool yes = false;
    bool no = false;
    int buttons = 0;
};

void check_move_project(const QJsonObject& measures, const QString& data_dir) {
    const QJsonObject spec = measures.value(QStringLiteral("move_project")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Move Project");

    msqt::MainWindow window;
    QString error;
    gate::check(window.open_file(data_dir + QStringLiteral("/synthetic-base.mpp"), &error),
                "the fixture opens for the Move Project gate");
    window.show();
    QApplication::processEvents();

    // No actual values: Project would not prompt, and neither do we.
    bool prompt_without_actuals = false;
    QTimer probe;
    probe.setInterval(10);
    QObject::connect(&probe, &QTimer::timeout, [&prompt_without_actuals]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (box != nullptr && box->isVisible()) {
                prompt_without_actuals = true;
                box->reject();
            }
        }
    });
    probe.start();
    window.show_move_project();
    QApplication::processEvents();
    probe.stop();
    gate::check(!prompt_without_actuals,
                "a project without actual values gets no Move Project prompt");

    // A project that carries actuals: open the Project-saved fixture in which
    // Task 1 was set to 50 % through MS Project itself (wave 27 W27-0.1).  The
    // 50 % ribbon command can queue the same edit now (wave 28 W28-1.2), but
    // this gate needs a FILE that already stores actuals on open, which only
    // the Project-saved fixture does.
    gate::check(window.open_file(
                    data_dir + QStringLiteral("/wave27-percent-actuals.mpp"), &error),
                "the Project-saved actuals fixture opens for the Move Project gate");
    QApplication::processEvents();
    gate::check(app::project_has_actual_values(window.plan()),
                "the Project-saved fixture carries an actual value");

    PromptSeen seen;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&seen, &timer]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (box == nullptr || !box->isVisible()) {
                continue;
            }
            seen.seen = true;
            seen.text = box->text();
            seen.yes = box->button(QMessageBox::Yes) != nullptr;
            seen.no = box->button(QMessageBox::No) != nullptr;
            seen.buttons = static_cast<int>(box->buttons().size());
            if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
                yes->click();
            }
            timer.stop();
            return;
        }
    });
    timer.start();
    window.show_move_project();
    QApplication::processEvents();
    timer.stop();
    gate::check(seen.seen, "the Move Project prompt is shown for a project with actuals");
    gate::check(seen.text == spec.value(QStringLiteral("text")).toString(),
                "the prompt carries Project's captured text verbatim");
    gate::check(seen.yes && seen.no && seen.buttons == 2,
                "the prompt offers exactly the captured Yes/No buttons");
    gate::check(window.pending_edits().empty(),
                "answering Yes queues no move edit and no other edit is pending");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: project_dialog_gate <w26-project-dialogs.measures.json> "
                     "<tests/data>\n");
        return 2;
    }
    const QJsonObject measures = load_measures(QString::fromLocal8Bit(argv[1]));
    gate::check(!measures.isEmpty(), "the measures file parses");
    const QString data_dir = QString::fromLocal8Bit(argv[2]);

    check_project_information(measures);
    check_change_working_time(measures, data_dir);
    check_custom_fields(measures);
    check_update_project(measures);
    check_links_between_projects(measures);
    check_ribbon_menus(measures, data_dir);
    check_move_project(measures, data_dir);

    std::printf("qt_project_dialog_gate: ");
    return gate::finish();
}
