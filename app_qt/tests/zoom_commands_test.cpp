// app_qt/tests/zoom_commands_test.cpp -- the View tab's Zoom commands through
// the real PlanDocument (wave 31, G2).
//
// W31-2.2: Entire Project and Selected Tasks fit the chart pane to the span
// Project 2021 actually shows.  The reference is
// tests/data/reference/w31-zoom.measures.json, measured by
// scripts/oracle_w31_zoom.py (pure COM) on the two committed fixtures:
//
//   fixture            command          Project window                    span
//   synthetic-base     Entire Project   2026-01-04 00:00 .. 01-09 20:57   4.958 d
//   synthetic-base     Selected (ID 3)  2026-01-04 06:00 .. 01-06 13:44   1.958 d
//   wave23-outline     Entire Project   2026-12-10 12:00 .. 12-19 03:51   7.375 d
//   wave23-outline     Selected (ID 3)  2026-12-11 00:00 .. 12-14 23:48   3.375 d
//
// Project draws the span at 85 % of its 1020 px chart pane (15 % margin,
// centred) and floors the window start to its bottom-tier column.  Its bottom
// tier is finer than a day, so several windows carry sub-day margins (base
// Selected starts at 06:00) that the port's day tier cannot express.  The gate
// therefore compares the MEASURED windows with the port's 85 % ideal within
// 1.5 %, then checks the port's own rule: the window is the larger of the
// ideal and the span from its floored origin, starts at or before the span,
// reaches the finish, and floors to Project's window day.
//
// The selection is the same task Project selected (ID 3, recorded by
// ActiveSelection in the oracle), not a row index: the port's sheet row and
// Project's view row need not number the same task.  Everything is session
// state: the fixture's bytes are hashed before and after and never change.
//
// The Zoom dialog's presets are also checked against the recorded windows:
// 1 week and 2 weeks are exact; the month presets use the stored-month mean
// (30.4375 days) that wave 29 measured for Project's month columns, 1.7 %
// short of the 1-month window and 1.3 % long of the 3-month one, so those two
// are held to 2 % with the reason recorded here.
//
// Usage: qt_zoom_commands <synthetic-base.mpp> <wave23-outline.mpp> <w31-zoom.measures.json>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "gantt.h"
#include "layout_model.h"
#include "plan_document.h"
#include "prefs.h"
#include "sheet.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

void check_near(double got, double want, double tolerance, const char* what) {
    ++g_checks;
    if (!(std::fabs(got - want) <= tolerance)) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s: got %.12f, want %.12f (+- %g)\n", what, got, want,
                     tolerance);
    }
}

void check_date(core::sched::date32 got, core::sched::date32 want, const char* what) {
    // A date32 packs the day with ticks of the day; these checks name a DAY
    // (the port's origins are midnight, while a span start keeps its time).
    const std::int32_t got_day = core::sched::day_of(got);
    const std::int32_t want_day = core::sched::day_of(want);
    ++g_checks;
    if (got_day != want_day) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s: got day %d, want day %d\n", what, got_day, want_day);
    }
}

QString file_hash(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

// "2026-01-04 13:00:00" -> the date32 day it names (time of day not needed: the
// port floors its origins to a day and the spans arrive as day counts).
core::sched::date32 date_of(const QString& text) {
    const QStringList parts = text.left(10).split('-');
    if (parts.size() != 3) {
        return core::sched::kDateNa;
    }
    return core::sched::from_ymd(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
}

QJsonObject find_case(const QJsonObject& measures, const QString& fixture) {
    for (const QJsonValue& value : measures.value("cases").toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry.value("fixture").toString() == fixture) {
            return entry;
        }
    }
    return QJsonObject();
}

double preset_days(const QJsonObject& measures, const QString& preset) {
    const QJsonArray presets = measures.value("dialog_presets").toObject().value("presets").toArray();
    for (const QJsonValue& value : presets) {
        const QJsonObject entry = value.toObject();
        if (entry.value("preset").toString() == preset) {
            return entry.value("days").toDouble();
        }
    }
    return 0.0;
}

const app::TaskRow* task_of_id(const msqt::PlanDocument& document, std::uint32_t id) {
    for (const app::TaskRow& task : document.plan().tasks) {
        if (task.id == id) {
            return &task;
        }
    }
    return nullptr;
}

std::size_t row_of_uid(const core::sheet::SheetModel& sheet, std::uint32_t uid) {
    for (std::size_t row = 0; row < sheet.row_count(); ++row) {
        if (sheet.row_uid(row) == uid) {
            return row;
        }
    }
    return core::sheet::kNoIndex;
}

// The window the document's timescale actually shows: width / pixels per day.
// The fit never uses the month unit for these spans, where pixels_per_day() is
// undefined.
struct FitWindow {
    core::sched::date32 origin = core::sched::kDateNa;
    double visible_days = 0.0;
    bool valid = false;
};

FitWindow window_of(const msqt::PlanDocument& document, double width_px) {
    FitWindow window;
    const core::gantt::Timescale& timescale = document.gantt().timescale;
    if (timescale.unit == core::gantt::TimescaleUnit::month || !timescale.valid()) {
        return window;
    }
    const double pixels_per_day = timescale.pixels_per_day();
    if (!(pixels_per_day > 0.0) || !std::isfinite(pixels_per_day)) {
        return window;
    }
    window.origin = timescale.origin;
    window.visible_days = width_px / pixels_per_day;
    window.valid = true;
    return window;
}

void check_window(const msqt::PlanDocument& document, const FitWindow& window, double ideal,
                  double measured_days, const char* rule, core::sched::date32 span_start,
                  core::sched::date32 span_finish, const QJsonObject& measure) {
    check(window.valid, rule);
    if (!window.valid) {
        return;
    }
    // Project's measured window is the 85 % ideal within 1.5 %.
    check(std::fabs(measured_days - ideal) / ideal <= 0.015,
          "Project's window is within 1.5 % of the 85 % ideal");
    // The port's window is the larger of the ideal and the span it must cover
    // from its floored origin.
    const double expected =
        std::max(ideal, app::span_days_between(window.origin, span_finish));
    check_near(window.visible_days, expected, 1e-6,
               "the port's window is the ideal or the floored span, whichever is larger");
    check(window.origin <= span_start, "the port's window starts at or before the span");
    check(window.visible_days + 1e-9 >= app::span_days_between(window.origin, span_finish),
          "the port's window reaches the span's finish");
    check_date(window.origin, date_of(measure.value("window_start").toString()),
               "the port floors its origin to Project's window day");
    // The document's own fit is active: a session timescale can only be this.
    check(document.session_timescale_active(), "the fit is session state");
}

void case_fit(const QString& path, const QString& fixture, const QJsonObject& measures) {
    const QString before = file_hash(path);
    check(!before.isEmpty(), "the fixture is readable");

    const QJsonObject entry = find_case(measures, fixture);
    check(!entry.isEmpty(), "the measures file records this fixture");
    if (entry.isEmpty()) {
        return;
    }

    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::day, error),
          "the fixture loads");
    if (!error.empty()) {
        std::fprintf(stderr, "note: %s: load error: %s\n", qPrintable(fixture), error.c_str());
        return;
    }

    core::sched::date32 project_start = core::sched::kDateNa;
    core::sched::date32 project_finish = core::sched::kDateNa;
    check(document.project_span(&project_start, &project_finish),
          "the project span is readable");
    const QJsonObject project_span = entry.value("project_span").toObject();
    check_near(app::span_days_between(project_start, project_finish),
               project_span.value("days").toDouble(), 1e-6,
               "the project span matches Project's measured span");
    check_date(project_start, date_of(project_span.value("start").toString()),
               "the project start is the measured one");
    check_date(project_finish, date_of(project_span.value("finish").toString()),
               "the project finish is the measured one");

    // ---- Entire Project -------------------------------------------------
    check(document.zoom_entire_project(1020.0, app::ZoomLevel::day),
          "Entire Project fits the chart pane");
    const FitWindow entire = window_of(document, 1020.0);
    const double entire_ideal =
        app::span_days_between(project_start, project_finish) / 0.85;
    const QJsonObject entire_measure = entry.value("entire").toObject();
    check_near(entire_measure.value("ideal_85_days").toDouble(), entire_ideal, 1e-6,
               "the reference's entire ideal is the measured span / 0.85");
    check_window(document, entire, entire_ideal,
                 entire_measure.value("visible_days").toDouble(),
                 "the entire-project fit has a linear-day window", project_start,
                 project_finish, entire_measure);

    // ---- Selected Tasks -------------------------------------------------
    const std::uint32_t selected_id =
        static_cast<std::uint32_t>(entry.value("selected_task_id").toInt());
    const app::TaskRow* task = task_of_id(document, selected_id);
    check(task != nullptr, "the measured task is in the plan");
    if (task == nullptr) {
        document.clear_session_timescale();
        return;
    }
    const std::size_t row = row_of_uid(*document.sheet(), task->uid);
    check(row != core::sheet::kNoIndex, "the measured task has a sheet row");
    if (row == core::sheet::kNoIndex) {
        document.clear_session_timescale();
        return;
    }
    check(document.sheet()->select_cell(row, 0), "the measured task's row selects");
    check(document.selected_uid() == task->uid, "the measured task is the selection");

    core::sched::date32 selected_start = core::sched::kDateNa;
    core::sched::date32 selected_finish = core::sched::kDateNa;
    check(document.selected_span(&selected_start, &selected_finish),
          "the selected span is readable");
    const QJsonObject selected_span = entry.value("selected_span").toObject();
    check_near(app::span_days_between(selected_start, selected_finish),
               selected_span.value("days").toDouble(), 1e-6,
               "the selected span matches Project's measured span");
    check_date(selected_start, date_of(selected_span.value("start").toString()),
               "the selected start is the measured one");
    check_date(selected_finish, date_of(selected_span.value("finish").toString()),
               "the selected finish is the measured one");

    check(document.zoom_selected_tasks(1020.0, app::ZoomLevel::day),
          "Selected Tasks fits the chart pane");
    const FitWindow selected = window_of(document, 1020.0);
    const double selected_ideal =
        app::span_days_between(selected_start, selected_finish) / 0.85;
    const QJsonObject selected_measure = entry.value("selected").toObject();
    check_near(selected_measure.value("ideal_85_days").toDouble(), selected_ideal, 1e-6,
               "the reference's selected ideal is the measured span / 0.85");
    check_window(document, selected, selected_ideal,
                 selected_measure.value("visible_days").toDouble(),
                 "the selected-tasks fit has a linear-day window", selected_start,
                 selected_finish, selected_measure);

    // No task row selected: Project's command falls back to the whole project.
    document.sheet()->clear_selection();
    check(!document.sheet()->has_selection(), "the selection clears");
    check(document.zoom_selected_tasks(1020.0, app::ZoomLevel::day),
          "Selected Tasks falls back with no selection");
    const FitWindow fallback = window_of(document, 1020.0);
    check(fallback.valid, "the fallback fit has a linear-day window");
    if (fallback.valid) {
        check_date(fallback.origin, entire.origin, "the fallback origin is the entire origin");
        check_near(fallback.visible_days, entire.visible_days, 1e-9,
                   "the fallback window is the entire-project window");
    }

    document.clear_session_timescale();
    check(!document.session_timescale_active(), "clearing drops the session timescale");
    check(file_hash(path) == before, "the zoom commands never change the file's bytes");
}

// The Zoom dialog's fixed spans, against the measured Project windows.  The
// anchor is the base fixture's open timescale start, 2026-10-11, the origin
// Project's dialog used for the week presets.
void case_dialog(const QString& path, const QJsonObject& measures) {
    const QString before = file_hash(path);
    check(!before.isEmpty(), "the base fixture is readable for the dialog case");

    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::day, error),
          "the base fixture loads for the dialog case");
    if (!error.empty()) {
        std::fprintf(stderr, "note: dialog load error: %s\n", error.c_str());
        return;
    }

    const core::sched::date32 anchor = core::sched::from_ymd(2026, 10, 11);

    check(document.zoom_fixed_span(anchor, 7.0, 1020.0, app::ZoomLevel::day),
          "the 1-week preset fits");
    const FitWindow week = window_of(document, 1020.0);
    check(week.valid, "the 1-week preset has a linear-day window");
    if (week.valid) {
        check_date(week.origin, anchor, "the 1-week preset starts at the anchor");
        check_near(week.visible_days, 7.0, 1e-9, "the 1-week preset shows 7 days");
    }
    check_near(preset_days(measures, "1week"), 7.0, 1e-9,
               "Project measured the 1-week preset as 7 days");

    check(document.zoom_fixed_span(anchor, 14.0, 1020.0, app::ZoomLevel::day),
          "the 2-week preset fits");
    const FitWindow two_weeks = window_of(document, 1020.0);
    check(two_weeks.valid, "the 2-week preset has a linear-day window");
    if (two_weeks.valid) {
        check_date(two_weeks.origin, anchor, "the 2-week preset starts at the anchor");
        check_near(two_weeks.visible_days, 14.0, 1e-9, "the 2-week preset shows 14 days");
    }
    check_near(preset_days(measures, "2weeks"), 14.0, 1e-9,
               "Project measured the 2-week preset as 14 days");

    // Project's month presets are anchored on the view it was showing; only
    // the window LENGTH is compared (see the header comment).
    const double project_month = preset_days(measures, "1month");
    check(document.zoom_fixed_span(anchor, app::kZoomMonthDays, 1020.0, app::ZoomLevel::day),
          "the 1-month preset fits");
    const FitWindow month = window_of(document, 1020.0);
    check(month.valid, "the 1-month preset has a linear-day window");
    if (month.valid) {
        check_near(month.visible_days, app::kZoomMonthDays, 1e-9,
                   "the 1-month preset shows one stored mean month");
    }
    check(std::fabs(app::kZoomMonthDays - project_month) / project_month <= 0.02,
          "the mean-month preset is within 2 % of Project's 1-month window");

    const double project_quarter = preset_days(measures, "3months");
    check(document.zoom_fixed_span(anchor, 3.0 * app::kZoomMonthDays, 1020.0,
                                   app::ZoomLevel::day),
          "the 3-month preset fits");
    const FitWindow quarter = window_of(document, 1020.0);
    check(quarter.valid, "the 3-month preset has a linear-day window");
    if (quarter.valid) {
        check_near(quarter.visible_days, 3.0 * app::kZoomMonthDays, 1e-9,
                   "the 3-month preset shows three stored mean months");
    }
    check(std::fabs(3.0 * app::kZoomMonthDays - project_quarter) / project_quarter <= 0.02,
          "the three-mean-month preset is within 2 % of Project's 3-month window");

    document.clear_session_timescale();
    check(!document.session_timescale_active(), "clearing drops the dialog timescale");
    check(file_hash(path) == before, "the dialog presets never change the file's bytes");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: qt_zoom_commands <synthetic-base.mpp> <wave23-outline.mpp> "
                     "<w31-zoom.measures.json>\n");
        return 2;
    }
    QFile measures_file(QString::fromLocal8Bit(argv[3]));
    if (!measures_file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read the measures file\n");
        return 2;
    }
    const QJsonObject measures = QJsonDocument::fromJson(measures_file.readAll()).object();
    check(!measures.isEmpty(), "the measures file parses");

    case_fit(QString::fromLocal8Bit(argv[1]), "synthetic-base.mpp", measures);
    case_fit(QString::fromLocal8Bit(argv[2]), "wave23-outline.mpp", measures);
    case_dialog(QString::fromLocal8Bit(argv[1]), measures);

    if (g_failures == 0) {
        std::printf("qt_zoom_commands: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    std::printf("qt_zoom_commands: %d checks, %d FAILURES\n", g_checks, g_failures);
    return 1;
}
