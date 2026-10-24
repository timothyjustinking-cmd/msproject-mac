// core/gantt/gantt.h -- the Gantt chart's geometry and state, with no drawing.
//
// This is the portable half of the Gantt view: a macOS shell (AppKit / Core
// Graphics) asks for rectangles, polylines, header cells and hit-test answers
// and draws them; it never computes a date-to-x mapping itself.  Nothing here
// includes a windowing system, allocates a graphics context or knows about
// points/DPI (see "Coordinates" below).
//
// WHAT IS EVIDENCED AND WHAT IS NOT
// ---------------------------------
// No oracle was used for the Gantt's own geometry (spec/ui-inventory.md §8: it
// is not in the resources at all).  The shape of the model follows the product:
// a two-tier timescale, one bar per task row, elbow-routed links with
// FS/SS/FF/SF attachment sides, a status date marker and percent-complete fills.
// The *numbers* (row height, bar height, link gap, ...) are our defaults, not
// measured from Project, and each place where a choice could have gone either
// way says "decision:" in the comment so a later oracle run knows what to test.
// See memory/gantt.md for the ranking of what that run should settle.
//
// Shading is the one part fed by MEASURED data: a core::sched::Calendar built
// by core/cal from the .mpp's decoded calendars (dated exceptions and work-week
// overrides included).  It still never moves a bar -- the axis stays calendar
// days; it only says which x spans are non-working.
//
// COORDINATES
// -----------
// Two independent spaces, both in pixels, both with y growing downwards (the
// Core Graphics default):
//
//   * bar/row space: x = 0 at the timescale origin (the snapped start of the
//     chart), y = 0 at the top of the first task row.  Header height is NOT
//     included: the shell subtracts the header band height when converting a
//     viewport point, and adds it when drawing.
//   * header space: HeaderCell::x is in bar/row space (same axis), and the
//     shell stacks the two tiers vertically (tier major above tier minor).
//
// The timescale is an infinite axis: header cells and non-working spans are
// generated for the x window the shell currently shows, not stored per layout.
//
// DAY GRANULARITY
// ---------------
// A bar always spans whole day columns: its left edge is 00:00 of the start day
// and its right edge is 00:00 of the day after the finish day.  The chart's axis
// is CALENDAR days, not working days (see Timescale and the note in
// memory/gantt.md): a Fri-Thu task that spends 5 working days draws seven day
// columns wide, with the weekend visible inside it.  Decision, and why, in
// Timescale's comment.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "date.h"

namespace core::gantt {

using core::sched::date32;
using core::sched::is_na;
using core::sched::kDateNa;

// ---------------------------------------------------------------------------
// Small value types
// ---------------------------------------------------------------------------

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    double right() const noexcept { return x + w; }
    double bottom() const noexcept { return y + h; }
    double center_x() const noexcept { return x + w / 2.0; }
    double center_y() const noexcept { return y + h / 2.0; }

    bool empty() const noexcept { return w <= 0.0 || h <= 0.0; }

    // `tolerance` grows the rectangle on every side; 0 is an exact test.
    bool contains(double px, double py, double tolerance = 0.0) const noexcept {
        return px >= x - tolerance && px <= right() + tolerance &&
               py >= y - tolerance && py <= bottom() + tolerance;
    }
};

// Distances and helpers that the headers/tests use; defined in gantt.cpp.
double distance(const Point& a, const Point& b) noexcept;
double distance_to_segment(const Point& p, const Point& a, const Point& b) noexcept;

// ---------------------------------------------------------------------------
// Working week (legacy shading source; a real Calendar supersedes it)
// ---------------------------------------------------------------------------

// Index 0 = Sunday, matching core::sched::weekday().  The default is Project's
// stock Monday-Friday week.  This is the fallback for callers that have no
// core::sched::Calendar; see "Non-working shading sources" below for precedence.
struct WorkingWeek {
    std::array<bool, 7> working{{false, true, true, true, true, true, false}};

    bool is_working(std::int32_t weekday) const noexcept {
        return weekday >= 0 && weekday < 7 && working[static_cast<std::size_t>(weekday)];
    }

    static WorkingWeek monday_to_friday() noexcept { return WorkingWeek{}; }
    static WorkingWeek all_days() noexcept {
        WorkingWeek week;
        week.working = {{true, true, true, true, true, true, true}};
        return week;
    }
};

// ---------------------------------------------------------------------------
// Timescale
// ---------------------------------------------------------------------------

enum class TimescaleUnit : std::uint8_t { day, week, month };

struct Timescale;  // defined below; the zoom helpers return/reference it

// The three zoom levels the brief calls for.  This is a naming/defaults layer on
// top of TimescaleUnit, not a second axis: timescale_for_zoom() returns an
// ordinary Timescale whose origin the caller can still snap or override.
enum class GanttZoom : std::uint8_t { day = 0, week, month };

struct ZoomSpec {
    GanttZoom zoom = GanttZoom::day;
    TimescaleUnit unit = TimescaleUnit::day;
    double pixels_per_unit = 20.0;  // decision: day 20 px/day, week 140 px/week, month 300 px/month
    const char* name = "day";
    // What the two header tiers say at this zoom.  Descriptive metadata for the
    // shell and the tests; Timescale::header() produces the actual strings.
    const char* minor_header = "day number";
    const char* major_header = "month and year";
};

ZoomSpec zoom_spec(GanttZoom zoom) noexcept;
const char* zoom_name(GanttZoom zoom) noexcept;

// A default Timescale for the zoom: unit and pixels_per_unit from zoom_spec(),
// origin as supplied.  The origin is snapped by build_gantt(), as usual.
Timescale timescale_for_zoom(GanttZoom zoom,
                             date32 origin = core::sched::from_ymd(1984, 1, 2)) noexcept;

// The zoom that matches a Timescale's unit.  Unknown/invalid scales report day.
GanttZoom zoom_for_timescale(const Timescale& scale) noexcept;

// Tier major = the coarser band on top (month for day/week units, year for the
// month unit); tier minor = the unit columns (day, week, month).
enum class HeaderTier : std::uint8_t { major, minor };

struct HeaderCell {
    std::string label;
    date32 start = kDateNa;   // first day covered, inclusive
    date32 finish = kDateNa;  // last day covered, inclusive
    double x = 0.0;
    double width = 0.0;
    HeaderTier tier = HeaderTier::minor;
    // 0 = Sunday .. 6 = Saturday for a day-unit minor cell; -1 otherwise.
    std::int32_t weekday = -1;

    bool contains(double px) const noexcept { return px >= x && px < x + width; }
};

// The horizontal axis: a unit, an origin date, and pixels per unit.
//
// DATE -> X IS PIECEWISE LINEAR FOR MONTHS.  A day column is always
// pixels_per_unit wide, a week column always pixels_per_unit wide (seven day
// columns of pixels_per_unit/7 each), and a month column always
// pixels_per_unit wide even though months have 28..31 days.  That is what the
// real product's month timescale does (month columns are equal width); the
// consequence is that within a month a day is pixels_per_unit/days_in_month.
//
// CALENDAR DAYS, NOT WORKING DAYS (decision).  The chart's x axis is calendar
// time: Saturday and Sunday occupy a full day column each.  Reasons, in order:
//   1. the header must show the date sequence (day numbers, month names, week
//      dates); a five-day working axis would make "1 October" and "month"
//      labels meaningless and make the axis non-invertible on weekends;
//   2. the real Gantt keeps a weekend in the chart and shades it (working time
//      is data drawn on top of the calendar axis), so bars visibly span
//      non-working days -- a 5-working-day task is 7 columns wide;
//   3. dependency routing and the status date are calendar positions, so they
//      would desynchronise from the bars under a working-day axis.
// Working-day compression is therefore NOT modelled here; non-working days are
// returned as x spans by GanttLayout::nonworking_spans() for the shell to shade.
// (Inference about the real product's default Gantt; no oracle run.)
struct Timescale {
    TimescaleUnit unit = TimescaleUnit::day;
    // First instant shown at x = 0; always a unit start at 00:00 after
    // snap_origin().  Default is the day after the engine epoch (1984-01-02 is
    // a Monday, so the week unit cannot snap below day 1); build_gantt()
    // replaces it.
    date32 origin = core::sched::from_ymd(1984, 1, 2);
    double pixels_per_unit = 20.0;
    // Week columns start on this weekday (0 = Sunday .. 6 = Saturday).  Monday
    // is the default (ISO-8601); MS Project's stock calendar week starts on
    // Sunday, so this is a documented choice, not evidence.  When a GanttSource
    // carries a calendar, build_gantt() adopts that calendar's own week anchor
    // instead (see adopt_calendar_week_start).
    std::int32_t week_start = 1;
    // When a calendar is supplied for shading, take the week anchor from the
    // calendar's effective week (core::sched models the week as Sunday-anchored,
    // which is also MS Project's stock week) rather than from `week_start`.
    // Set false to keep the caller's week_start with a calendar present.
    // decision: the no-calendar default stays Monday (above); the calendar path
    // is data-driven because the calendar is the only source of week data here.
    bool adopt_calendar_week_start = true;

    bool valid() const noexcept { return pixels_per_unit > 0.0 && !is_na(origin); }

    // Start of the unit column containing `d`: 00:00 of the day (day), the most
    // recent week_start at 00:00 (week), or 00:00 on the 1st (month).
    date32 unit_start(date32 d) const;

    // Snap the origin so no bar can start left of it: it moves backwards to the
    // unit start containing `earliest` when that is earlier than the current
    // origin, and otherwise simply normalises the current origin to a unit
    // start.  Never moves right, so a caller-chosen origin stays a lower bound.
    void snap_origin(date32 earliest);

    // Exact instant -> x.  Time of day is honoured (a date at 12:00 sits half a
    // day into its column).  NA/overflow returns NaN.
    double x_for_date(date32 d) const;

    // 00:00 of the day containing `d` -> x (the bar/grid position).  NaN if NA.
    double x_for_day_start(date32 d) const;

    // x -> 00:00 of the day that contains it.  The axis is unbounded in both
    // directions (x < 0 gives days before the origin).  NaN input -> NA.
    date32 date_at_x(double x) const;

    // Header labels covering [x0, x1); cells that start before x0 or end after
    // x1 are returned whole (the shell clips).  Empty when x1 <= x0 or invalid.
    std::vector<HeaderCell> header(HeaderTier tier, double x0, double x1) const;

    // Width of one day column, valid for the day and week units.  For the month
    // unit this is NaN because the width changes at every month boundary.
    double pixels_per_day() const noexcept;
};

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------

struct RowRange {
    int first_row = 0;
    int row_count = 0;

    bool empty() const noexcept { return row_count <= 0; }
    int end_row() const noexcept { return first_row + row_count; }  // exclusive
};

struct RowLayout {
    double row_height = 22.0;

    double y_for_row(int row) const noexcept { return static_cast<double>(row) * row_height; }

    // -1 for negative or non-finite y (there is no row above the first one), and
    // when row_height is not positive.
    int row_at_y(double y) const noexcept;

    // Slice of rows intersecting [scroll_y, scroll_y + viewport_height), clamped
    // to row_count.  Partially visible rows are included on both ends.
    RowRange visible_rows(double scroll_y, double viewport_height,
                          int row_count) const noexcept;
};

// ---------------------------------------------------------------------------
// Bars
// ---------------------------------------------------------------------------

enum class BarKind : std::uint8_t { normal, summary, milestone };

// How the shell should draw the bar.  `rect` remains the authoritative box; the
// extra points in Bar let a renderer draw the diamond / bracket exactly.
// decision (unmeasured): the summary bracket is two small downward triangles at
// the bar's lower outer corners.  The product's exact caps are not measured.
enum class BarShape : std::uint8_t { rectangle = 0, diamond, summary_bracket };

struct LabelPlacement {
    bool visible = false;
    bool inside = false;  // true: drawn within the bar; false: to its right
    Rect rect;
};

// Wave 29 (W29-1.3): which side of the bar a stored bar style's label sits on,
// in the order the style record stores the five field ids (left, right, top,
// bottom, inside).
enum class BarLabelSide : std::uint8_t { left = 0, right, top, bottom, inside };

inline constexpr std::size_t kBarLabelSideCount = 5;

// One label the applied view's stored bar styles call for.  The layout places
// it (left/right aligned to the bar's edge, top/bottom above/below it, inside
// within it); the shell only draws the text in `rect`.
struct BarText {
    BarLabelSide side = BarLabelSide::right;
    bool inside = false;  // true: drawn over the bar, in the bar-text colour
    Rect rect;
    std::string text;
};

struct Bar {
    std::uint32_t task_uid = 0;
    int row = 0;
    BarKind kind = BarKind::normal;
    bool critical = false;
    double percent_complete = 0.0;  // clamped to 0..1
    Rect rect;                      // full bar; diamond bounding box for a milestone
    Rect fill;                      // completed portion (width 0 at 0 %)
    LabelPlacement label;
    std::string label_text;
    // Appended (additive).  shape is redundant with kind but makes the drawing
    // contract explicit.  diamond = 4 outline points (top, right, bottom, left)
    // for a milestone; cap_left/cap_right = 3-point triangles for a summary.
    BarShape shape = BarShape::rectangle;
    std::array<Point, 4> diamond{};
    std::array<Point, 3> cap_left{};
    std::array<Point, 3> cap_right{};
    // Wave 25 (W25-2/S2): copied from TaskBarInput::manual.
    bool manual = false;
    // Wave 29 (W29-1.3): the labels the applied view's stored bar styles call
    // for, in BarLabelSide order.  Empty when the caller supplied none; the
    // single `label`/`label_text` above is then the fallback.
    std::vector<BarText> texts;
};

// The per-task input.  `finish` is the task's finish *instant* (the real
// product's, e.g. 17:00 on the last day); the bar covers that whole day, so a
// 1-day task is one full column and a 5-day task across a weekend is 7 columns.
struct TaskBarInput {
    std::uint32_t uid = 0;
    std::string label;
    date32 start = kDateNa;
    date32 finish = kDateNa;  // NA is treated as `start`
    double percent_complete = 0.0;
    BarKind kind = BarKind::normal;
    bool critical = false;
    // Appended: the resource text a resource-labelled bar shows.  The Gantt does
    // not know field ids, so the caller supplies it (usually ResourceNames).
    std::string resource;
    // Wave 25 (W25-2/S2): manually scheduled.  Project's stock Gantt paints a
    // manual task's bar and the links leaving it in its own colour ("Manual
    // Task"/"Manual Summary" bar styles), distinct from the auto-scheduled
    // "Task" style.  The model carries the flag; the shell picks the colour.
    bool manual = false;
    // Wave 29 (W29-1.3): the text the applied view's stored bar styles put on
    // each side of the bar, in BarLabelSide order.  The caller formats them
    // (the Gantt does not know field ids or the file's date format).
    std::array<std::string, kBarLabelSideCount> sides{};
    // True when `sides` is the stored answer, even if every entry is empty: the
    // bar then shows exactly those labels and NONE of the bar_label_text
    // fallback (a stored style list with no label means a bare bar).
    bool stored_labels = false;
};

// ---------------------------------------------------------------------------
// Links
// ---------------------------------------------------------------------------

enum class LinkType : std::uint8_t {
    finish_to_start,  // FS
    start_to_start,   // SS
    finish_to_finish, // FF
    start_to_finish,  // SF
};

struct LinkInput {
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    LinkType type = LinkType::finish_to_start;
    // Driving/critical hint from the scheduler (core::sched::cpm); the Gantt
    // does not derive criticality itself.
    bool critical = false;
    // Wave 25 (W25-2/S2): the predecessor's scheduling mode.  Project colours a
    // link from the predecessor's bar style, so a link leaving a manual task is
    // the Manual Task link colour.
    bool manual = false;
};

struct ArrowHead {
    Point tip;                        // exactly on the successor's bar edge
    Point direction;                  // unit vector the arrow points along
    Point wing[2];                    // the two base corners
};

struct LinkRoute {
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    LinkType type = LinkType::finish_to_start;
    bool critical = false;
    // Orthogonal polyline: points.front() is on the predecessor's edge,
    // points.back() == arrow.tip is on the successor's edge.  Consecutive
    // points are never coincident.
    std::vector<Point> points;
    ArrowHead arrow;
    Rect bounds;  // tight bounding box of the polyline
    // Wave 25 (W25-2/S2): copied from LinkInput::manual.  APPENDED so a future
    // positional aggregate initialiser keeps its field order.
    bool manual = false;
};

// ---------------------------------------------------------------------------
// Layout style: all numbers are ours, not measured from Project
// ---------------------------------------------------------------------------

// Which text a bar shows.  The product's BarLabelDialog (UIFILE 4807) proves the
// feature exists but its defaults are not in the resources, so the set below is
// a decision (unmeasured); `name` is the compatible default.
enum class BarLabelContent : std::uint8_t {
    none = 0,
    name,
    resource,
    dates,
    percent,
    name_and_percent,
    name_and_resource,
    name_and_dates,
};

const char* bar_label_content_name(BarLabelContent content) noexcept;
bool bar_label_content_from_name(std::string_view name, BarLabelContent& out) noexcept;

struct GanttStyle {
    double row_height = 22.0;
    double bar_height = 12.0;
    // Summary bars are a thin rectangle plus two small end-cap triangles
    // (Bar::cap_left/cap_right).  The product's exact cap shape is unmeasured;
    // this is a decision, not evidence.
    double summary_height = 8.0;
    double summary_cap_width = 4.0;
    double summary_cap_height = 4.0;
    // The diamond's bounding-box side.  decision (settled): the diamond is
    // centred in its own day column, not on the date line -- see gantt.cpp.
    // At a scale whose day column is narrower than this (month zoom), the
    // diamond shrinks to the column so it never spills into a neighbour.
    double milestone_size = 12.0;
    double label_gap = 4.0;        // bar edge -> label, and inside padding
    // Fallback text measurement when measure_text is unset: 7 px per UTF-8
    // code point.  The macOS shell will set measure_text to a real font metric.
    double label_char_width = 7.0;
    double link_gap = 8.0;        // horizontal stub / clearance for elbows
    double arrow_length = 6.0;
    double arrow_width = 4.0;
    double hit_tolerance = 3.0;   // fixed fallback; see hit_tolerance_days below

    // Optional: measured width of a label in pixels.  When empty, the fallback
    // above is used.  This is the only callback in the model and it is stdlib
    // (no UI dependency).
    std::function<double(std::string_view)> measure_text;

    // Appended (additive).  Bar label content and zoom-aware hit testing.
    BarLabelContent bar_label = BarLabelContent::name;
    // Tolerance follows the day-column width: day/week 20 px * 0.15 = 3 px
    // (the old fixed value), month 9.68 px * 0.15 -> clamped to the 2 px floor.
    // decision (unmeasured); set hit_tolerance_days to 0 for the fixed value.
    double hit_tolerance_days = 0.15;
    double min_hit_tolerance = 2.0;
    double max_hit_tolerance = 8.0;
};

// Effective hit tolerance for a timescale (see GanttStyle::hit_tolerance_days).
double hit_test_tolerance(const Timescale& timescale, const GanttStyle& style) noexcept;

// The text a bar shows for `content`.  `dates` is formatted from the task's
// start/finish (ISO "YYYY-MM-DD", or "start - finish" when they differ);
// `percent` is the clamped percent rounded to a whole number.  A missing name
// yields an empty string, so the label is simply not placed.
std::string bar_label_text(const TaskBarInput& task, const GanttStyle& style);

// ---------------------------------------------------------------------------
// Non-working shading sources
//
// Shading never moves a bar.  The layout can be told what the working time is in
// three ways, in this precedence order:
//
//   1. `calendar`  -- a real core::sched::Calendar (usually built by
//      core::cal::load_project_calendar()/build_calendar()).  It is the only
//      source that knows a day is only PARTLY working (a 4-hour Friday), and it
//      already carries dated exceptions and work-week overrides: core/cal merges
//      every dated exception into the Week of the week it falls in and installs
//      it with Calendar::set_work_week, so the Gantt sees them through
//      Calendar::pattern_for_day() without knowing the .mpp format.
//   2. `working_day` -- a caller-supplied day predicate, whole-day granularity.
//   3. the legacy pair `working_week` + `nonworking_days` -- used when neither
//      of the above is set, so every existing caller keeps working unchanged.
//
// `nonworking_days` is applied on top of whichever source is selected: a listed
// day is always shaded as a whole non-working day.  `working_week` is ignored
// when a calendar or a predicate is supplied (it would contradict the source).
// ---------------------------------------------------------------------------

// Caller-supplied whole-day predicate: true when the day has any working time.
// `day` is midnight of the day.  It must outlive the layout if it captures by
// reference; GanttSource::set_calendar() avoids that by owning a calendar copy.
using WorkingDayPredicate = std::function<bool(date32)>;

struct GanttSource {
    std::vector<TaskBarInput> tasks;  // index = row, even when a task has no bar
    std::vector<LinkInput> links;
    // Legacy shading source (used only when `calendar` and `working_day` are
    // both unset).  Kept so existing callers do not change.
    WorkingWeek working_week{};
    std::vector<date32> nonworking_days;  // forced non-working days, any order
    date32 status_date = kDateNa;         // vertical status/progress marker

    // Real working-time calendar; takes precedence over the legacy sources.
    // Only non-working spans and is_working_day() read it.
    std::shared_ptr<const core::sched::Calendar> calendar;
    // Whole-day predicate fallback when there is no calendar.
    WorkingDayPredicate working_day;

    // Convenience: copy `source_calendar` into the layout-owning shared_ptr and
    // set `calendar`.  The copy is what makes it safe for the layout to outlive
    // the source (nonworking_spans() is called later, while drawing).  Clears
    // `working_day` because the calendar supersedes it.
    void set_calendar(const core::sched::Calendar& source_calendar);
};

struct NonworkingSpan {
    date32 date = kDateNa;
    double x = 0.0;
    double width = 0.0;
    // True when the span covers only the non-working part of a partly working
    // day (from a calendar's day pattern), false for a whole day column.
    bool partial = false;
};

enum class HitKind : std::uint8_t { none, bar, link };

struct HitResult {
    HitKind kind = HitKind::none;
    std::uint32_t task_uid = 0;  // kind == bar
    std::size_t link_index = static_cast<std::size_t>(-1);  // kind == link
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    LinkType link_type = LinkType::finish_to_start;
    bool critical = false;
};

// ---------------------------------------------------------------------------
// The whole layout
// ---------------------------------------------------------------------------

struct GanttLayout {
    Timescale timescale;  // origin already snapped by build_gantt()
    RowLayout rows;
    GanttStyle style;
    std::vector<Bar> bars;   // one per drawable task, in row order
    std::vector<LinkRoute> links;
    int row_count = 0;
    double content_width = 0.0;  // right edge of the last bar (not the label)
    WorkingWeek working_week{};
    std::vector<date32> nonworking_days;  // normalised, sorted, unique
    date32 status_date = kDateNa;
    double status_x = std::numeric_limits<double>::quiet_NaN();
    bool has_status = false;
    // Appended last so no existing positional aggregate initialisation shifts.
    // Copied from the source; `calendar` (when set) is authoritative for
    // shading and `working_day` is the whole-day fallback.
    std::shared_ptr<const core::sched::Calendar> calendar;
    WorkingDayPredicate working_day;

    bool has_status_line() const noexcept { return has_status; }

    const Bar* bar(std::uint32_t uid) const;

    RowRange visible_rows(double scroll_y, double viewport_height) const {
        return rows.visible_rows(scroll_y, viewport_height, row_count);
    }

    // Bars first (any bar whose rect contains the point within
    // style.hit_tolerance), then links (point-to-polyline distance within the
    // same tolerance); the first match wins.  Task ids are UniqueIDs.
    HitResult hit_test(double x, double y) const;

    // True when the layout's shading source gives `day` any working time.
    // Precedence: a forced `nonworking_days` entry is always non-working; then
    // the calendar (any working time at all); then the working_day predicate;
    // then the legacy weekend mask.  NA/overflow is not a working day.
    bool is_working_day(date32 day) const;

    // x spans of every NON-WORKING part of a day column intersecting [x0, x1).
    // The source is chosen as documented above GanttSource:
    //   * calendar: the day's working intervals are complemented, so a day with
    //     no working time is ONE whole-day span (partial == false), while every
    //     non-working stretch INSIDE a working day is its own partial span
    //     (partial == true): a normal 08:00-17:00 day has 00:00-08:00,
    //     12:00-13:00 and 17:00-24:00, and a 4-hour Friday 08:00-12:00 has
    //     00:00-08:00 and 12:00-24:00.  A day-granularity renderer should draw
    //     only spans with partial == false; an hour-granularity one draws all.
    //     This is the only source that can express a partly working day.
    //   * working_day: whole-day spans where the predicate is false;
    //   * otherwise: Saturday/Sunday by `working_week` plus `nonworking_days`.
    // `nonworking_days` always shades its day whole (no partial spans for it).
    // Spans come back in date then x order, each separated from the next by at
    // least one working instant.
    std::vector<NonworkingSpan> nonworking_spans(double x0, double x1) const;

    std::vector<HeaderCell> header(HeaderTier tier, double x0, double x1) const {
        return timescale.header(tier, x0, x1);
    }
};

// Build the layout.  The timescale's unit and pixels-per-unit are taken from
// `timescale`; its origin is snapped backwards over the earliest task start so
// no bar can be clipped at x = 0.  The caller sets `origin` to the project
// start (or any date at or after the first task); it is only ever moved
// earlier.  Links whose endpoints have no bar (missing task, NA start) are
// skipped.
//
// The shading source (calendar / predicate / legacy mask) is copied into the
// layout and never affects bar geometry.  With a calendar present the week
// anchor is taken from it unless `timescale.adopt_calendar_week_start` is false.
GanttLayout build_gantt(const GanttSource& source, const GanttStyle& style = {},
                        const Timescale& timescale = {});

}  // namespace core::gantt
