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
//   view              which of the four view modes the window shows (app/
//                     view_model.h): Task Sheet, Resource Sheet, Task Usage,
//                     Resource Usage.
//   usage_period      the bucket width of the two usage grids: day/week/month
//                     (core::usage::Period).  It is view state, exactly as the
//                     Gantt timescale is; nothing is written to a .mpp.
//   formula_duration_column / formula_date_column
//                     the default visibility of the two demo computed columns of
//                     the task sheet (app::formula_column_specs()).  They can
//                     only be shown/hidden, never edited: the columns are
//                     read-only and the save path writes task starts only.
//   stored_custom_fields
//                     the DEFAULT visibility of the six stored custom-field
//                     columns (Text1/Number1/Number2/Date1/Duration1/Flag1).
//                     Off by default: the reader decodes the values (wave 12),
//                     but six more columns would crowd the sheet, so the
//                     View > Columns chooser shows them hidden.
//   column_visibility (wave 12)
//                     a per-column override list keyed by field id, so a stored
//                     custom field or a formula column can be shown/hidden
//                     individually and the choice survives a relaunch.  Only
//                     columns whose state differs from their default are stored,
//                     which keeps the line short and lets a new column arrive
//                     visible.
//
// PERSISTENCE FORMAT
//   One line: "version=1;date=day_month_year;duration=auto_units;zoom=day;
//   shading=1;view=task_sheet;usage=day;formula1=1;formula2=1;custom=0;
//   columns=0x0b400033:1,0x0b400057:0".
//   parse_preferences() is deliberately tolerant: an unknown key is ignored, a
//   malformed value keeps the default, and missing keys keep the default, so an
//   old or hand-edited defaults string can never stop the app from launching.
//   It returns true when at least one known key was read.  The wave-11 and
//   wave-12 keys are additive: an older string without them parses and keeps the
//   new defaults.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gantt.h"
#include "sheet.h"
#include "usage.h"
#include "view_model.h"

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

// One persisted "this column is shown/hidden" decision, keyed by the PjField id
// the column addresses.  Only the columns a preference owns (the stored custom
// fields and the formula columns) are ever recorded; the identity columns stay
// in the column set.
struct ColumnVisibility {
    core::view::FieldId field = 0;
    bool visible = true;
};

struct Preferences {
    DateStyle date_style = DateStyle::day_month_year;
    DurationStyle duration_style = DurationStyle::auto_units;
    ZoomLevel zoom = ZoomLevel::day;
    bool working_day_shading = true;
    // Wave 11 view state.  `view` is the mode the window opens in; `usage_period`
    // is the day/week/month bucket width of the two usage grids; the formula
    // flags are the default visibility of the two demo formula columns (app::
    // formula_column_specs()).  `stored_custom_fields` is the default visibility
    // of the six stored custom-field columns: the reader decodes the values
    // (wave 12), but the columns start hidden so the sheet stays narrow.
    ViewMode view = ViewMode::task_sheet;
    core::usage::Period usage_period = core::usage::Period::day;
    bool formula_duration_column = true;
    bool formula_date_column = true;
    bool stored_custom_fields = false;
    // Wave 12: per-column overrides for the stored/formula columns, in the order
    // they were first changed.  Bounded by kMaxColumnVisibility.
    std::vector<ColumnVisibility> column_visibility;
    // Stamped into the stored string so a later change of meaning is
    // detectable.  parse_preferences() accepts any version it knows.
    int version = 1;
};

inline constexpr int kPreferencesVersion = 1;

// The most overrides the stored line keeps; a longer list drops the tail (a
// hand-edited defaults string cannot make the line unbounded).
inline constexpr std::size_t kMaxColumnVisibility = 64;

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

// Whether the formula column at `index` (into app::formula_column_specs(): 0 is
// Duration x2, 1 is Start +7) is visible.  An index past the two formula columns
// is always visible: this helper only owns the computed columns.  A per-column
// override (see below) wins over the two flags.
bool formula_column_visible(const Preferences& preferences, std::size_t index) noexcept;
// Stores one formula column's visibility; false (changing nothing) when the
// index is not one of the two formula columns.
bool set_formula_column_visible(Preferences& preferences, std::size_t index,
                                bool visible) noexcept;

// The visibility of one stored/formula column: the override when one is
// recorded, else `default_visible`.  The shell passes the stored columns'
// default as Preferences::stored_custom_fields and a formula column's as true.
bool column_visible(const Preferences& preferences, core::view::FieldId field,
                    bool default_visible) noexcept;

// Records one column's visibility.  A choice equal to the default removes the
// override, so the stored line only carries real deviations.  False (changing
// nothing) for field 0 or when the override list is full.
bool set_column_visible(Preferences& preferences, core::view::FieldId field, bool visible,
                        bool default_visible) noexcept;

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
