// app/usage_grid.h -- Task Usage and Resource Usage as Project draws them
// (wave 31, G4).
//
// WHAT THIS IS
// ------------
// A logical-row model over the wave-11 plan: parent rows (tasks in Task Usage,
// resources in Resource Usage) followed by one assignment row under each,
// exactly the row order the wave-31 captures measured (tests/data/reference/
// wave31-usage.measures.json, rendered_grid.*.logical_rows):
//
//   Task Usage      task, assignment (labelled with the RESOURCE), ...
//   Resource Usage  resource, assignment (labelled with the TASK), ...
//
// Each logical row carries one value series per stored DETAIL and one integer
// value per period column.  A detail is a line Project draws inside the row:
// Work, Actual Work or Cost, in the file's stored order (0x2240003c; decoded by
// core/viewstate/usage_style.h).  A file that stores no usage view, or no
// Details list, gets the one default Work detail the brief names.
//
// VALUE RULES (measured on tests/data/wave31-usage.mpp through COM
// TimeScaleData, committed in wave31-usage.measures.json)
// ------------------------------------------------------------------------
//   * Work: periods' working minutes, from core/usage's flat spread of the
//     assignment's stored Work.  This is exact for the fixture's contour-less
//     assignments (480 min per working day) and for every flat assignment; a
//     contoured assignment is spread flat (core/usage documents the decision).
//   * Actual Work: Work x the task's stored percent complete, with cumulative
//     rounding so the row total is round(work_total * pct / 100).  Exact for
//     0 % and 100 % (the fixture measures both); for a partial percent this is
//     the documented approximation -- Project places actual work by its own
//     status-date/contour rule, which this port does not decode.
//   * Cost: Work x the resource's stored standard rate, in cents, rounded per
//     cell.  The fixture's resources all Accrue At Prorated (3), the mode this
//     rule matches; overtime, Cost/Use and the Start/End accrual modes are not
//     phased this wave (a documented residual, no fixture measures them).
//     Cost does NOT scale with percent complete -- the measured Cost buckets
//     are the full scheduled cost on 0 % tasks.
//
// The row total for Work is the assignment's stored Work, even when some of it
// could not be placed on the calendar (core/usage decision 4); the period cells
// then sum to less than the total, as they do in Project.
//
// TIMESCALE
// ---------
// The columns come from the stored BOTTOM tier (the rendered row below the
// week/month group, decoded as UsageViewStyle::minor): days, weeks or months
// with the stored count.  A week column starts on Monday (Project's week
// boundary; the wave-31 captures' "Wk Mon 19/10/26" label).  The first and last
// columns are clipped to the assignment span, as Project clips them.  A stored
// unit this wave cannot draw (minutes, hours, quarters, ...) falls back to days
// with a warning; the shell keeps the file's own header text (W31-4.4).
//
// TOTALS
// ------
// Every row carries its own per-detail total.  A final "Total" row (kind
// `total`) sums every assignment row; it is a PORT AFFORDANCE -- the wave-31
// captures measured that Project's usage sheets have neither a total row nor a
// total column -- and W31-4.4 paints it as the existing wave-20 sheet did.
//
// This is presentation data built from an in-memory plan.  Names never leave
// memory: labels are the plan's own strings and are never written anywhere.

#ifndef APP_USAGE_GRID_H
#define APP_USAGE_GRID_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "layout_model.h"
#include "usage.h"
#include "usage_style.h"
#include "view_style.h"

namespace app {

enum class UsageGridRowKind : std::uint8_t { task, resource, assignment, total };

// One rendered detail line: its value rule and Project's label.
struct UsageGridDetail {
    msproject::viewstate::UsageDetailKind kind =
        msproject::viewstate::UsageDetailKind::work;
    std::string label;  // "Work", "Actual Work", "Cost"
    bool cost = false;  // values are cents, not minutes
};

struct UsageGridRow {
    UsageGridRowKind kind = UsageGridRowKind::task;
    std::uint32_t uid = 0;         // task/resource/assignment uid; 0 for Total
    std::uint32_t parent_uid = 0;  // assignment: its task (Task Usage) or
                                   // resource (Resource Usage) uid
    std::size_t parent_index = 0;  // assignment: index of its parent row;
                                   // otherwise the row's own index
    bool child = false;            // an assignment sub-row (indent 1)
    // The frozen gutter's number.  Measured on the wave-31 captures: Project's
    // usage sheets number the parent task/resource rows with their stored ID
    // (1, 2, 3) and leave every assignment row's gutter blank.  The port's
    // Total row (an affordance Project has no counterpart for) is blank too.
    std::uint32_t display_id = 0;  // Task.ID / Resource.ID for a parent row
    bool numbered = false;         // parents true; assignments and Total false
    std::string label;
    // values[detail][period], totals[detail]; both sized from the grid's
    // details.  Work/Actual Work are minutes, Cost is cents.
    std::vector<std::vector<std::int64_t>> values;
    std::vector<std::int64_t> totals;
};

struct UsageGrid {
    bool resource = false;  // false = Task Usage, true = Resource Usage
    bool stored_style = false;  // details/timescale came from the file
    // The column granularity and the stored count (1 for the default).
    core::usage::Period period = core::usage::Period::day;
    int periods_per_column = 1;
    // The rendered header tiers: the stored MIDDLE tier is the TOP row and the
    // stored BOTTOM tier the bottom row (core/viewstate/usage_style.h).  The
    // wave-31 fixture stores weeks/days with formats 13/20 and alignments
    // left/centre; W31-4.4 renders exactly those shapes.  An undecoded file
    // gets weeks over the fallback period, and a stored unit this wave cannot
    // draw is replaced (with a warning in `warnings`).
    msproject::viewstate::TimescaleTier major_tier;
    msproject::viewstate::TimescaleTier minor_tier;
    std::vector<core::usage::PeriodSpan> periods;
    std::vector<UsageGridDetail> details;
    std::vector<UsageGridRow> rows;  // parents+children, then the Total row
    // Style and timescale notes worth surfacing in the shell; empty when the
    // file's choices are drawn exactly.
    std::vector<std::string> warnings;
};

// Build the grid from `input` with the view's stored presentation.  `style`
// may be default-constructed (undecoded): the grid then shows the Work detail
// over `fallback_period` columns (day unless the shell passes its Day/Week/Month
// choice), the brief's default.  A DECODED style's own bottom tier wins over
// `fallback_period` -- the file's stored timescale is the fidelity target.
// `calendar` phases Work (the fixture uses the standard week; the shell may
// pass the file's own calendar).
[[nodiscard]] UsageGrid make_usage_grid(
    const PlanInput& input, const app::UsageViewStyle& style, bool resource,
    const core::sched::Calendar& calendar,
    core::usage::Period fallback_period = core::usage::Period::day);

}  // namespace app

#endif  // APP_USAGE_GRID_H
