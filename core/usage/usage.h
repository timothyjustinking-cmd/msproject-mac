// core/usage/usage.h -- time-phased work buckets for the usage views (W43).
//
// WHAT THIS IS
// ------------
// A period table (day / week / month) over a date range, plus work minutes
// distributed into it.  Three projections are built from one assignment list:
//
//   * assignment-usage -- one row per assignment, minutes per period;
//   * task-usage       -- one row per task, the sum of that task's assignments;
//   * resource-usage   -- one row per resource, the sum of its assignments.
//
// There is no user interface here: the result is plain data with integer
// counts and minutes, so a renderer (the app's usage sheet, CSV, a test) reads
// the same numbers.
//
// WHERE THE NUMBERS COME FROM (MEASURED, docs/analysis/oracle/w10-oracle.md §3)
// ---------------------------------------------------------------------------
// MS Project does NOT store a per-day work array.  The oracle's flat vs
// back-loaded vs actual fixture diff showed no stream growing by a time-phased
// array: only one assignment row plus its metadata.  Assignment totals live in
// TBkndAssn/FixedData:
//
//   +12  double  Units, x10000 (10000.0 == 100 %)
//   +20  double  Work, minutes x1000 (2,400,000 == 2,400 min == 5 days)
//   +52  u32     Start   (packed date, same layout as core::sched::date32)
//   +56  u32     Finish  (packed date)
//   +60  u32     Resume  (packed date; not Stop)
//
// and the work contour is a step list in assignment VarMeta:
//
//   0x0f400031  planned contour steps (flat = 72 B, back-loaded = 212 B)
//   0x0f400032  actual work steps
//
// DECODING 0x0f400031 IS NOT IMPLEMENTED (OPTIONAL IN THIS STREAM).  A flat
// spread is exact for every contour-less assignment, which is what the reader
// exposes today.  The flat spread here distributes the assignment's total work
// over the assignment's *working time* in proportion to each period's working
// ticks, so weekends, holidays and the file's own work week get no buckets.
// The step-list decode would be an improvement for back-loaded/front-loaded
// contours only, and any such decode would be inference: no oracle JSON pairs
// the step bytes with TimeScaleData buckets.  Flat remains the fallback either
// way.
//
// CALENDAR
// --------
// Working time comes from core::sched::Calendar (work_time_between), never from
// a reimplementation here.  The caller supplies the calendar: a file's own
// project calendar (core::cal::load_project_calendar) or a synthetic standard
// week in tests.  Assignment Start/Finish are packed date32 values, and the
// session memory records that a packed date maps to date32 by identity -- no
// day shift.
//
// DECISIONS (no oracle measurement; keep them in one place)
// ---------------------------------------------------------
//   1. A week period starts on Sunday (weekday 0), matching the engine's
//      weekday numbering (spec/scheduling.md §0.1).  No fixture measures the
//      usage timescale's week boundary; this is a decision, not a finding.
//   2. A month period is a civil month; the first/last period may be clipped
//      by the requested range.
//   3. Minutes are integers.  The flat spread uses cumulative rounding so the
//      bucket cells sum to the assignment's Work exactly (the test pins this).
//   4. Work on an assignment whose dates are unusable (NA, finish <= start, or
//      no working time in the span) cannot be phased.  It stays in the row
//      total and in UsageProjection::total_minutes but contributes no bucket;
//      the assignment is counted in UsageProjection::unplaced_assignments.
//   5. The task/resource tables have exactly one row per uid in
//      UsageInput::task_uids / resource_uids, in that order.  An assignment
//      whose task/resource uid is not in the supplied list (a deleted task in
//      PROJECT_B, say) still appears in UsageProjection::assignments and in
//      total_minutes, but invents no row in those tables.  A resource uid of 0
//      means "no resource" (the reader's null sentinel) and produces no
//      resource row either.

#ifndef CORE_USAGE_USAGE_H
#define CORE_USAGE_USAGE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "date.h"

namespace core::usage {

using sched::date32;

// The bucket width of a period row.
enum class Period : std::uint8_t { day = 0, week = 1, month = 2 };

const char* period_name(Period period) noexcept;
bool period_from_name(std::string_view name, Period& out) noexcept;

// One inclusive run of calendar days.  Day numbers are the engine's date32 day
// field (1 = 1984-01-01).  A clipped week/month is still one PeriodSpan.
struct PeriodSpan {
    std::int32_t first_day = 0;
    std::int32_t last_day = 0;

    bool valid() const noexcept { return last_day >= first_day && first_day >= 1; }
    bool contains(std::int32_t day) const noexcept {
        return day >= first_day && day <= last_day;
    }
    std::int32_t day_count() const noexcept {
        return valid() ? last_day - first_day + 1 : 0;
    }
    // Date-only packed values for display and for calendar clipping.
    date32 first_date() const noexcept;
    date32 last_date() const noexcept;
    // Day after last_day at 00:00, the half-open end of the span.
    date32 end_date() const noexcept;
};

// Enumerate the periods of `period` covering [first_day, last_day].  Empty when
// the range is empty or outside the day domain.  Periods are clipped to the
// requested range, so the first and last spans can be partial.
std::vector<PeriodSpan> enumerate_periods(Period period, std::int32_t first_day,
                                          std::int32_t last_day);

// Civil-date convenience.  NA/overflow dates yield an empty vector.
std::vector<PeriodSpan> enumerate_periods(Period period, date32 first, date32 last);

// One assignment as this module needs it.  Build it from mpp::Assignment with
// usage_mpp.h, or directly in tests.  All values are neutral: no reader type
// appears in this header.
struct AssignmentInput {
    std::uint32_t uid = 0;           // assignment UniqueID
    std::uint32_t task_uid = 0;      // task UniqueID
    std::uint32_t resource_uid = 0;  // resource UniqueID; 0 = no resource
    date32 start = sched::kDateNa;
    date32 finish = sched::kDateNa;
    // Assignment Work in minutes (mpp::Assignment::work_minutes()).  Negative
    // values are treated as zero.
    std::int64_t work_minutes = 0;
};

// The complete input to one projection.  Task/resource uid lists carry table
// order (the reader's FixedData order); a uid that only appears in an
// assignment is appended in encounter order.
struct UsageInput {
    std::vector<std::uint32_t> task_uids;
    std::vector<std::uint32_t> resource_uids;
    std::vector<AssignmentInput> assignments;
};

// One row: an identity plus one integer minute count per period.  minutes
// always has the same length as the table's period list.
struct UsageRow {
    std::uint32_t uid = 0;
    // Authoritative total from the input assignments; may exceed the sum of
    // `minutes` only when some work was unplaceable (see decision 4).
    std::int64_t work_minutes = 0;
    std::vector<std::int64_t> minutes;
};

// One projection over one period list.
struct UsageTable {
    Period period = Period::day;
    std::vector<PeriodSpan> periods;
    std::vector<UsageRow> rows;
    // Sum of UsageRow::work_minutes over rows.
    std::int64_t total_minutes = 0;

    int row_of_uid(std::uint32_t uid) const noexcept;
    const UsageRow* row(std::uint32_t uid) const noexcept;
    std::int64_t minutes_at(std::size_t row_index, std::size_t period_index) const noexcept;
    std::int64_t row_total(std::size_t row_index) const noexcept;
    std::int64_t period_total(std::size_t period_index) const noexcept;
    // Largest single cell; 0 for an empty table.
    std::int64_t peak_minutes() const noexcept;
};

// One assignment's row, including the ids so a caller can attribute it.
struct AssignmentUsage {
    std::uint32_t uid = 0;
    std::uint32_t task_uid = 0;
    std::uint32_t resource_uid = 0;
    std::int64_t work_minutes = 0;
    std::int64_t unplaced_minutes = 0;
    std::vector<std::int64_t> minutes;
};

// The three projections over one period list.
struct UsageProjection {
    Period period = Period::day;
    std::vector<PeriodSpan> periods;
    std::vector<AssignmentUsage> assignments;
    UsageTable tasks;
    UsageTable resources;
    // Sum of all assignment Work in the input.
    std::int64_t total_minutes = 0;
    // Assignments with work that had no placeable working time (decision 4).
    std::size_t unplaced_assignments = 0;
};

// Build the projection.  The period list covers the earliest start through the
// latest finish among the placeable assignments; an input with no placeable
// assignment gets an empty period list (and therefore all-zero buckets).
UsageProjection project_usage(const UsageInput& input, const sched::Calendar& calendar,
                              Period period = Period::day);

// A name-free, line-oriented rendering for logs and tests:
//
//   usage period=day periods=4 rows=2 total_minutes=960
//   row uid=1 total=960: 2026-10-16=480 2026-10-19=480
//
// Only non-zero cells are listed.  No client name is ever part of this module,
// so the output is safe to print.
std::string render_usage_table(const UsageTable& table);

}  // namespace core::usage

#endif  // CORE_USAGE_USAGE_H
