// app/prefs.h -- the shell's preferences: what they are, how they persist, and
// what they change on screen.
//
// WHY APP-SIDE (measured, not assumed)
//   `docs/analysis/oracle/w10-oracle.md` section 4 measured Project's own stores:
//   the table's date format is a property of the TABLE DEFINITION (written into
//   `114/Props`, with Project's own `214/`/`Props14` churn), and there is no
//   exposed setter for the GLOBAL date format, duration format or Gantt
//   timescale (`Application.DateFormat`/`DurationFormat` are conversion
//   functions, `Application.Timescale`/`Options*` are dialog methods).  So the
//   shell persists its preferences in `NSUserDefaults` (the macOS side) as one
//   string produced by serialize_preferences(), and never pretends they are
//   going into a .mpp.  Writing the per-table date format through `114/Props`
//   would need a Props decoder this wave does not have; it is not faked.
//
// WHAT A PREFERENCE CHANGES
//   date_style        the sheet's date rendering (core::sheet::DateFormat), so
//                     Start/Finish cells, the status line and the PDF report all
//                     agree; applied through apply_preferences().
//   duration_style    the shell's duration rendering.  "auto_units" is the
//                     sheet's own convention (largest unit that fits: "3 days",
//                     "4 hrs"); "hours" and "days" are app-side renderings that
//                     every surface the shell prints goes through
//                     (format_duration(), cell_text()).
//   zoom              the Gantt timescale (day/week/month columns), applied
//                     through app::timescale_for_zoom() in layout_model.h.
//   working_day_shading
//                     draw the Gantt's non-working shading or not.  Shading is
//                     purely the shell's (core/gantt computes the spans; who
//                     draws them is the shell's choice).
//
// PERSISTENCE FORMAT
//   One line: "version=1;date=day_month_year;duration=auto_units;zoom=day;shading=1".
//   parse_preferences() is deliberately tolerant: an unknown key is ignored, a
//   malformed value keeps the default, and missing keys keep the default, so an
//   old or hand-edited defaults string can never stop the app from launching.
//   It returns true when at least one known key was read.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "gantt.h"
#include "sheet.h"

namespace app {

// The NSUserDefaults key the macOS shell stores serialize_preferences() under.
inline constexpr const char* kPreferencesDefaultsKey =
    "com.msprojectdecompile.shell.preferences";

enum class DateStyle : std::uint8_t {
    day_month_year = 0,  // "Tue 20/10/26" (Project's short date, the default)
    month_day_year,      // "Tue 10/20/26"
    iso,                 // "2026-10-20"
};

enum class DurationStyle : std::uint8_t {
    auto_units = 0,  // the sheet's own: largest unit that fits ("3 days", "4 hrs")
    hours,           // every duration in hours ("24 hrs", "0.5 hrs")
    days,            // every duration in working days ("3 days", "0.5 days")
};

enum class ZoomLevel : std::uint8_t {
    day = 0,
    week,
    month,
};

struct Preferences {
    DateStyle date_style = DateStyle::day_month_year;
    DurationStyle duration_style = DurationStyle::auto_units;
    ZoomLevel zoom = ZoomLevel::day;
    bool working_day_shading = true;
    // Stamped into the stored string so a later change of meaning is
    // detectable.  parse_preferences() accepts any version it knows.
    int version = 1;
};

inline constexpr int kPreferencesVersion = 1;

// Stable identifiers, used in the stored string and in the status line.  They
// are part of the on-disk contract: do not rename one without a migration.
const char* date_style_name(DateStyle style) noexcept;
const char* duration_style_name(DurationStyle style) noexcept;
const char* zoom_level_name(ZoomLevel level) noexcept;
bool date_style_from_name(std::string_view name, DateStyle& out) noexcept;
bool duration_style_from_name(std::string_view name, DurationStyle& out) noexcept;
bool zoom_level_from_name(std::string_view name, ZoomLevel& out) noexcept;

// The DateFormat a preference asks for.  The three forms are the core's own
// builders, so the sheet, the PDF report and this file cannot disagree.
core::sheet::DateFormat date_format_for(const Preferences& preferences) noexcept;

// Writes the preference's presentation choices into `format`.  Today that is the
// date format (every date cell, the status line and the PDF report read
// SheetFormat); the duration style is applied by format_duration()/cell_text()
// because core/sheet owns no duration-unit switch.  Idempotent.
void apply_preferences(const Preferences& preferences, core::sheet::SheetFormat& format) noexcept;

// The Gantt zoom a preference asks for.
core::gantt::GanttZoom gantt_zoom(ZoomLevel level) noexcept;
// The inverse, for a shell that has a Timescale and wants to show its level.
ZoomLevel zoom_level_of(core::gantt::GanttZoom zoom) noexcept;

// The one line stored in NSUserDefaults (see the header comment).
std::string serialize_preferences(const Preferences& preferences);
bool parse_preferences(std::string_view text, Preferences& out);

// A human-readable one-liner for the status bar and the About panel.
std::string preferences_summary(const Preferences& preferences);

// Duration ticks rendered per `style`.  auto_units calls
// core::sheet::format_duration_text() (one implementation, the sheet's);
// hours/days divide by one hour / one working day (format.hours_per_day) and
// never invent a unit the core does not know.  0 ticks renders "0 days"/"0 hrs".
std::string format_duration(std::int64_t ticks, const core::sheet::SheetFormat& format,
                            DurationStyle style);

// The text the shell draws in one cell: the sheet's own cell_text(), except for
// a Duration cell when the preference asks for hours or days, where the
// app-side rendering above is used.  Editing is unaffected (the sheet parses
// input itself), and an empty cell/NULL value stays empty.
std::string cell_text(const core::sheet::SheetModel& sheet, std::size_t row,
                      std::size_t column, const Preferences& preferences);

}  // namespace app
