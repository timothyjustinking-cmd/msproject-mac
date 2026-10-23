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
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTabWidget>
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

    std::printf("qt_project_dialog_gate: ");
    return gate::finish();
}
