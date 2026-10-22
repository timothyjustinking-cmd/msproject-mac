// app/prefs_tests.cpp -- the preferences model, proved headlessly.
//
// Covers: the stable identifiers (they are part of what NSUserDefaults stores),
// the tolerant parser (a hand-edited or older defaults string must never stop
// the app launching), the date styles through the core's own DateFormat, the
// app-side duration renderings, cell_text() choosing the app rendering only for
// the Duration column, the zoom mapping, and the summary line.
//
// Synthetic rows only: no corpus, no client data, no file I/O.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "date.h"
#include "gantt.h"
#include "layout_model.h"
#include "prefs.h"
#include "sheet.h"
#include "value.h"

namespace {

using app::DateStyle;
using app::DurationStyle;
using app::Preferences;
using app::ZoomLevel;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}


// A four-task plan with fixed, synthetic values.
app::PlanInput make_plan() {
    app::PlanInput input;
    input.title = "Synthetic plan";
    input.project_start = core::sched::from_ymd(2026, 10, 19);
    const char* names[] = {"One", "Two", "Three", "Four"};
    for (std::size_t index = 0; index < 4; ++index) {
        app::TaskRow row;
        row.uid = static_cast<std::uint32_t>(index + 1);
        row.id = static_cast<std::uint32_t>(index + 1);
        row.outline_number = std::to_string(index + 1);
        row.name = names[index];
        row.duration_ticks = static_cast<std::int64_t>(index + 1) * core::view::kTicksPerDay;
        row.start = core::sched::from_ymd(2026, 10, 19 + static_cast<int>(index));
        row.finish = row.start;
        row.outline_level = 0;
        input.tasks.push_back(row);
    }
    return input;
}

void test_identifiers() {
    for (const DateStyle style : {DateStyle::day_month_year, DateStyle::month_day_year,
                                  DateStyle::iso}) {
        check(app::date_style_name(style)[0] != '\0', "date style has a name");
        DateStyle parsed = DateStyle::iso;
        check(app::date_style_from_name(app::date_style_name(style), parsed),
              "date style name round-trips");
        check(parsed == style, "date style parsed back");
    }
    for (const DurationStyle style :
         {DurationStyle::auto_units, DurationStyle::hours, DurationStyle::days}) {
        DurationStyle parsed = DurationStyle::hours;
        check(app::duration_style_from_name(app::duration_style_name(style), parsed),
              "duration style name round-trips");
        check(parsed == style, "duration style parsed back");
    }
    for (const ZoomLevel level : {ZoomLevel::day, ZoomLevel::week, ZoomLevel::month}) {
        ZoomLevel parsed = ZoomLevel::month;
        check(app::zoom_level_from_name(app::zoom_level_name(level), parsed),
              "zoom name round-trips");
        check(parsed == level, "zoom parsed back");
    }
    DateStyle date_out = DateStyle::iso;
    check(!app::date_style_from_name("Day", date_out), "identifiers are case-sensitive");
    ZoomLevel zoom_out = ZoomLevel::day;
    check(!app::zoom_level_from_name("", zoom_out), "an empty name is not a zoom");
}

void test_parse_and_serialize() {
    Preferences prefs;
    prefs.date_style = DateStyle::month_day_year;
    prefs.duration_style = DurationStyle::hours;
    prefs.zoom = ZoomLevel::week;
    prefs.working_day_shading = false;

    const std::string text = app::serialize_preferences(prefs);
    check(text.find("version=1") != std::string::npos, "the stored string carries a version");
    check(text.find("date=month_day_year") != std::string::npos, "date style is stored by name");
    check(text.find("duration=hours") != std::string::npos, "duration style is stored by name");
    check(text.find("zoom=week") != std::string::npos, "zoom is stored by name");
    check(text.find("shading=0") != std::string::npos, "shading is stored as 0/1");

    Preferences parsed;
    check(app::parse_preferences(text, parsed), "a written string parses");
    check(parsed.date_style == prefs.date_style, "date style round-trips");
    check(parsed.duration_style == prefs.duration_style, "duration style round-trips");
    check(parsed.zoom == prefs.zoom, "zoom round-trips");
    check(parsed.working_day_shading == prefs.working_day_shading, "shading round-trips");

    // Defaults: an empty or foreign string leaves every default in place.
    Preferences defaults;
    check(!app::parse_preferences("", parsed), "an empty string reads nothing");
    check(parsed.date_style == defaults.date_style, "empty string keeps the default date style");
    check(parsed.zoom == defaults.zoom, "empty string keeps the default zoom");

    // Tolerant: unknown keys and malformed values are ignored, known ones read.
    check(app::parse_preferences(
              "future=1; date=iso ; duration=nonsense; shading=maybe; zoom=month", parsed),
          "a string with some known keys parses");
    check(parsed.date_style == DateStyle::iso, "a known key after whitespace is read");
    check(parsed.duration_style == defaults.duration_style, "a bad value keeps the default");
    check(parsed.working_day_shading == defaults.working_day_shading, "a bad flag keeps the default");
    check(parsed.zoom == ZoomLevel::month, "the later known key is read");
}

void test_date_format() {
    core::sheet::SheetFormat format;
    core::view::DateValue date;
    date.year = 2026;
    date.month = 10;
    date.day = 20;

    Preferences prefs;
    app::apply_preferences(prefs, format);
    check_text(core::sheet::format_date_text(date, format), "Tue 20/10/26",
               "the default date style is Project's short day/month/year");

    prefs.date_style = DateStyle::month_day_year;
    app::apply_preferences(prefs, format);
    check_text(core::sheet::format_date_text(date, format), "Tue 10/20/26",
               "month/day/year reorders the date");

    prefs.date_style = DateStyle::iso;
    app::apply_preferences(prefs, format);
    check_text(core::sheet::format_date_text(date, format), "2026-10-20",
               "the ISO style prints an unambiguous date");
}

void test_duration_rendering() {
    core::sheet::SheetFormat format;  // 8-hour day, "day"/"hrs" labels
    check_text(app::format_duration(3 * core::view::kTicksPerDay, format,
                                    DurationStyle::auto_units),
               "3 days", "auto units are the sheet's own rendering");
    check_text(app::format_duration(3 * core::view::kTicksPerDay, format, DurationStyle::hours),
               "24 hrs", "3 working days are 24 hours");
    check_text(app::format_duration(4 * core::view::kTicksPerHour, format, DurationStyle::hours),
               "4 hrs", "4 hours stay 4 hrs");
    check_text(app::format_duration(4 * core::view::kTicksPerHour, format, DurationStyle::days),
               "0.5 days", "4 hours is half of an 8-hour working day");
    check_text(app::format_duration(2 * core::view::kTicksPerHour, format, DurationStyle::days),
               "0.25 days", "2 hours is a quarter of an 8-hour working day");
    check_text(app::format_duration(core::view::kTicksPerDay, format, DurationStyle::days),
               "1 day", "one working day is singular");
    check_text(app::format_duration(0, format, DurationStyle::hours), "0 hrs",
               "zero hours renders without a singular label");
    check_text(app::format_duration(0, format, DurationStyle::days), "0 days",
               "zero days renders without a singular label");
    check_text(app::format_duration(3 * core::view::kTicksPerHour, format, DurationStyle::hours),
               "3 hrs", "a three-hour task reads 3 hrs");

    // A different working-day length changes the day rendering, not the hour one.
    core::sheet::SheetFormat ten_hour;
    ten_hour.hours_per_day = 10;
    check_text(app::format_duration(5 * core::view::kTicksPerHour, ten_hour,
                                    DurationStyle::days),
               "0.5 days", "a 5-hour day is half of a 10-hour working day");
    check_text(app::format_duration(5 * core::view::kTicksPerHour, ten_hour,
                                    DurationStyle::hours),
               "5 hrs", "hours do not depend on the working-day length");
}

void test_cell_text() {
    app::PlanInput input = make_plan();
    core::view::RecordSet records = app::make_record_set(input);
    core::sheet::SheetModel sheet(records, app::default_sheet_columns());
    sheet.set_format(core::sheet::SheetFormat{});

    std::size_t duration_column = core::sheet::kNoIndex;
    std::size_t name_column = core::sheet::kNoIndex;
    for (std::size_t column = 0; column < sheet.column_count(); ++column) {
        if (sheet.column(column).field == app::task_field_id("Duration")) {
            duration_column = column;
        }
        if (sheet.column(column).field == app::name_field_id()) {
            name_column = column;
        }
    }
    check(duration_column != core::sheet::kNoIndex, "the default columns include Duration");
    check(name_column != core::sheet::kNoIndex, "the default columns include Name");
    if (duration_column == core::sheet::kNoIndex || name_column == core::sheet::kNoIndex) {
        return;
    }

    Preferences prefs;  // auto units: identical to the sheet's own text
    check_text(app::cell_text(sheet, 0, duration_column, prefs),
               sheet.cell_text(0, duration_column), "auto units defer to the sheet");

    prefs.duration_style = DurationStyle::hours;
    check_text(app::cell_text(sheet, 0, duration_column, prefs), "8 hrs",
               "hours render the task's one-day duration as 8 hrs");
    check_text(app::cell_text(sheet, 0, name_column, prefs), sheet.cell_text(0, name_column),
               "the duration preference never changes a text column");
    check_text(app::cell_text(sheet, 1, duration_column, prefs), "16 hrs",
               "the second task's two days are 16 hrs");

    // A row whose Duration is NULL keeps the sheet's own (empty) text.
    records.set_field(2, app::task_field_id("Duration"), core::view::Value::null());
    check_text(app::cell_text(sheet, 2, duration_column, prefs), sheet.cell_text(2, duration_column),
               "a NULL duration falls back to the sheet");
}

void test_zoom_mapping() {
    check(app::gantt_zoom(ZoomLevel::day) == core::gantt::GanttZoom::day, "day maps to day");
    check(app::gantt_zoom(ZoomLevel::week) == core::gantt::GanttZoom::week, "week maps to week");
    check(app::gantt_zoom(ZoomLevel::month) == core::gantt::GanttZoom::month, "month maps to month");
    check(app::zoom_level_of(core::gantt::GanttZoom::week) == ZoomLevel::week,
          "the inverse mapping agrees");

    const app::PlanInput input = make_plan();
    for (const ZoomLevel level : {ZoomLevel::day, ZoomLevel::week, ZoomLevel::month}) {
        const core::gantt::Timescale scale =
            app::timescale_for_zoom(input, app::gantt_zoom(level));
        const core::gantt::ZoomSpec spec = core::gantt::zoom_spec(app::gantt_zoom(level));
        check(scale.unit == spec.unit, "the zoom's unit is the core's");
        check(scale.pixels_per_unit == spec.pixels_per_unit, "the zoom's width is the core's");
        check(!core::sched::is_na(scale.origin), "the zoom's origin is seeded from the plan");
        check(core::sched::day_of(scale.origin) == core::sched::day_of(input.project_start),
              "the zoom's origin is the project start");
    }
}

void test_summary() {
    Preferences prefs;
    prefs.working_day_shading = false;
    const std::string summary = app::preferences_summary(prefs);
    check(summary.find("day/month/year") != std::string::npos, "the summary names the date style");
    check(summary.find("days + hours") != std::string::npos,
          "the summary names the duration style");
    check(summary.find("timescale day") != std::string::npos, "the summary names the timescale");
    check(summary.find("shading off") != std::string::npos, "the summary names the shading state");
}

}  // namespace

int main() {
    test_identifiers();
    test_parse_and_serialize();
    test_date_format();
    test_duration_rendering();
    test_cell_text();
    test_zoom_mapping();
    test_summary();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("prefs_tests: %d checks passed\n", g_checks);
    return 0;
}
