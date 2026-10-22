// app/view_model.h -- the wave-11 views over the portable core: the resource
// sheet, the task/resource usage grids, and the task sheet's computed columns.
//
// WHY THIS FILE
//   main.mm is Objective-C++ and only the macOS CI job compiles it, so anything
//   that can be plain C++ lives outside it and is proved on Windows
//   (app/views_tests.cpp).  This header is that plain half for the views: it
//   owns the view-mode enum, maps app::PlanInput to a core::view::RecordSet for
//   the resource sheet, turns a core::usage::UsageProjection into the rows and
//   columns of a usage grid, and registers the task sheet's computed columns
//   through core::formula::ComputedColumns.  No AppKit, no reader types, no
//   file I/O.
//
// WHAT IT IS NOT
//   * It draws nothing.  main.mm draws a SheetModel, exactly as it does for the
//     task sheet; a resource/usage grid is only a different RecordSet + columns.
//   * It is read-only.  Every column this file produces is declared read-only
//     and nothing here writes a value back to a file.  Wave 12 owns editing.
//   * It does not schedule.  Usage buckets are computed by core/usage from the
//     assignment totals (docs/analysis/oracle/w10-oracle.md section 3: MS
//     Project stores no per-day array).
//
// THE CALENDAR (a documented choice)
//   core/usage needs a working-time calendar to spread an assignment.  The shell
//   uses the stock Monday-Friday 08:00-12:00 / 13:00-17:00 week
//   (standard_calendar()), the same week core/plan and tests/usage_tests.cpp
//   use.  The file's own project calendar exists (core::cal::
//   load_project_calendar) but is NOT wired this wave: it re-opens the .mpp, it
//   can throw on a calendar this reader does not decode, and a wrong calendar
//   silently changes every bucket.  A follow-up can replace the one call site
//   (app::standard_calendar()) with core::cal once that link is tested against
//   a fixture; the rest of this file does not care which calendar it gets.
//
// APP-PRIVATE FIELD IDS
//   A usage grid is an aggregation, not a PjField: its period columns have no
//   descriptor-table identity.  Those columns therefore use ids in the
//   0x0b4f0000 range, which core/model/field_table.gen.cpp leaves unused (the
//   1,444 Task fields end at 0x0b4005a3; app::kUidField already uses
//   0x0b4fff01 for the task sheet's UID column).  The ids never go near a file.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "columns.h"
#include "layout_model.h"
#include "record_set.h"
#include "resource_tables.h"
#include "sheet.h"
#include "usage.h"
#include "value.h"

namespace app {

// ---------------------------------------------------------------------------
// View modes
// ---------------------------------------------------------------------------

// Task Sheet is the existing sheet + Gantt.  The other three are grids: they
// have no Gantt and no cell selection (README "the views").
enum class ViewMode : std::uint8_t {
    task_sheet = 0,
    resource_sheet = 1,
    task_usage = 2,
    resource_usage = 3,
};

inline constexpr std::size_t kViewModeCount = 4;

// Stable identifiers for the persisted preference string; do not rename without
// a migration.  view_mode_title() is the menu/segment label.
const char* view_mode_name(ViewMode mode) noexcept;
const char* view_mode_title(ViewMode mode) noexcept;
bool view_mode_from_name(std::string_view name, ViewMode& out) noexcept;

// True for task_sheet; a task view owns the selection, editing and the Gantt.
bool view_mode_is_task(ViewMode mode) noexcept;
// True for task_usage and resource_usage.
bool view_mode_is_usage(ViewMode mode) noexcept;
// True only for task_sheet (the one mode with a chart).
bool view_mode_has_gantt(ViewMode mode) noexcept;
// True only for task_sheet (the one mode with a selection/cell editor).
bool view_mode_has_selection(ViewMode mode) noexcept;

// The usage period a mode starts with / a preference stores.  Day is the
// default; the period is only meaningful for the two usage modes.
core::usage::Period default_usage_period() noexcept;

// ---------------------------------------------------------------------------
// Calendar and the usage input
// ---------------------------------------------------------------------------

// The stock Monday-Friday 08:00-12:00 / 13:00-17:00 week (see the header
// comment).  Returned by value because core::sched::Calendar is copyable.
core::sched::Calendar standard_calendar();

// PlanInput -> core::usage::UsageInput.  The shell normally takes
// core::usage::usage_input_from_mpp(document) straight from the reader; this
// builder is the equivalent path for a PlanInput that did not come from a
// document (tests, a future importer), and views_tests.cpp pins that the
// assignment fields survive it.
core::usage::UsageInput usage_input_from_plan(const PlanInput& input);

// ---------------------------------------------------------------------------
// Resource sheet
// ---------------------------------------------------------------------------

// PlanInput::resources -> the neutral records core/view's builder takes.
std::vector<core::view::ResourceRecord> resource_records_from_plan(const PlanInput& input);

// The resource sheet's RecordSet.  Starts from
// core::view::build_resource_record_set() (the measured Entry-table schema:
// ID, Name, Type, Initials, MaxUnits) and then:
//   * replaces the Type integer with its display name ("Work"/"Material"/
//     "Cost"), because this view is read-only and the product shows the name;
//   * adds a Work column (Resource.Work, 0x0c40000d) holding the resource's
//     total work as duration ticks.  `work_by_resource` is
//     UsageProjection::resources for any period (the row total is
//     period-independent); null means "no Work column value", and the column
//     still exists so the schema does not depend on data.
// Cost is NOT shown: the reader decodes no resource rates, so a cost column
// would be invented (README "open").
core::view::RecordSet make_resource_view_records(const PlanInput& input,
                                                 const core::usage::UsageTable* work_by_resource);

// ID, Name, Type, Initials, MaxUnits in the registry's Entry-table order and
// widths, then Work.  Every column is read-only.  The first five resolve
// through core::view::find_resource_table("resource-entry"), so a registry
// change moves this view with it.
std::vector<core::sheet::SheetColumn> resource_sheet_columns();

// ---------------------------------------------------------------------------
// Usage views
// ---------------------------------------------------------------------------

// The neutral Resource.Work field id (table_registry.cpp writes it as the
// "Work" column of the resource Usage/Work tables).
inline constexpr core::view::FieldId kResourceWorkField = 0x0c40000du;

// App-private usage-grid field ids (see the header comment).
inline constexpr core::view::FieldId kUsageLabelField = 0x0b4f0001u;
inline constexpr core::view::FieldId kUsageTotalField = 0x0b4f0002u;
inline constexpr core::view::FieldId kUsagePeriodFieldBase = 0x0b4f1000u;
// Period columns are bounded so the synthetic ids stay clear of every other
// field: 1,024 covers a day view of a normal programme and every realistic
// week/month view.  Beyond this the row/period totals remain real, but the
// extra period columns are not created (README "open").
inline constexpr std::size_t kUsageMaxPeriodColumns = 1024;

// One usage grid, ready to render: the projection's table for the chosen
// projection plus the display labels.  `row_labels` and `period_labels` run
// parallel to table.rows / table.periods.  A label is the in-memory task or
// resource name; this struct is never written to a file.
struct UsageView {
    core::usage::Period period = core::usage::Period::day;
    bool resource = false;  // false = task usage, true = resource usage
    core::usage::UsageTable table;
    std::vector<std::string> row_labels;
    std::vector<std::string> period_labels;
    std::int64_t grand_total_minutes = 0;
};

// Picks projection.tasks or projection.resources and builds the labels:
//   * day   "Mon 19/10/26"      (the sheet's own date format)
//   * week  "Wk Mon 19/10/26"   (a week period is a clipped run of days)
//   * month "Oct 2026"
// A row whose uid has no name in `input` is labelled "task <uid>" or
// "resource <uid>" -- never a client name invented from anywhere.
UsageView make_usage_view(const core::usage::UsageProjection& projection, bool resource,
                          const PlanInput& input, const core::sheet::SheetFormat& format);

// The grid: the label column, one duration column per period, a Total column,
// then one TOTAL row (uid 0, "Total") whose cells are the period totals and
// whose Total cell is the table's total work.  Values are stored in ticks
// (minutes * 10) so the sheet's duration rendering -- including the user's
// duration-style preference -- formats them exactly as every other duration.
core::view::RecordSet make_usage_record_set(const UsageView& view);

// Label, period columns in period_labels order, Total -- every column
// read-only.  The caller's date/duration preferences flow through the normal
// core::sheet formatting.
std::vector<core::sheet::SheetColumn> usage_sheet_columns(const UsageView& view);

// Number of period columns make_usage_record_set / usage_sheet_columns create
// for a view (min(periods, kUsageMaxPeriodColumns)).
std::size_t usage_period_column_count(const UsageView& view) noexcept;

// ---------------------------------------------------------------------------
// Computed columns (task sheet)
// ---------------------------------------------------------------------------

// One formula column the View > Columns chooser can show or hide.  The field is
// a real PjField id (core::formula::ComputedColumns requires one); the formula
// text is written against the descriptor names, which is what Project's own
// formula language accepts.
struct FormulaColumnSpec {
    core::view::FieldId field = 0;
    const char* title = "";
    const char* formula = "";
};

// The two formula columns this wave offers:
//   Number1 = [Duration]*2   ("Duration x2", a Duration)
//   Date1   = [Start]+7      ("Start +7 Days", a Date; 7 calendar days later)
// Field ids resolve through the descriptor table by name, so a table
// regeneration moves them; a name the table lost drops that column rather than
// pointing it at a guess.  Static (process lifetime).
const std::vector<FormulaColumnSpec>& formula_column_specs();

// Which formula column (index into formula_column_specs()) a sheet column
// address toggles, or -1 when the column is not a computed one.  Used by the
// shell's View > Columns persistence.
int formula_column_index(core::view::FieldId field) noexcept;

// default_sheet_columns() plus one column per formula spec, hidden/shown by the
// caller and declared read-only here.  The task sheet's column set is FIXED
// (the same 9 columns on every load), so the View > Columns submenu's tag =
// model column index stays valid across loads and view switches.
std::vector<core::sheet::SheetColumn> task_sheet_columns();

// Registers every formula spec in `out`.  False when a spec does not parse (the
// caller reports `error`; the app should keep going with the columns that did
// register -- this returns false on the first failure, having added the rest is
// not attempted).  The set starts empty: callers build a fresh one per load.
bool build_computed_columns(core::formula::ComputedColumns& out, std::string& error);

// Marks every computed column of `sheet` read-only.  Belt and braces next to
// task_sheet_columns(), because a caller may hand the sheet a plain
// default_sheet_columns() list (the existing tests do) and then add the formula
// columns itself.
void apply_computed_read_only(core::sheet::SheetModel& sheet);

}  // namespace app
