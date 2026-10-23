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
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QFile>
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
#include <QWidget>

#include <cmath>
#include <cstdio>
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

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: project_dialog_gate <w26-project-dialogs.measures.json>\n");
        return 2;
    }
    const QJsonObject measures = load_measures(QString::fromLocal8Bit(argv[1]));
    gate::check(!measures.isEmpty(), "the measures file parses");

    check_project_information(measures);

    std::printf("qt_project_dialog_gate: ");
    return gate::finish();
}
