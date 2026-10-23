// core/session/session.h -- the document/session model: a .mpp on disk becomes
// everything a UI needs, with no AppKit and no Objective-C.
//
// ONE JOB.  `load(path)` runs the whole portable pipeline once:
//
//     core/mpp  reader            .mpp -> Document (tasks, links, resources,
//                                        assignments, project row)
//     core/cal  decoder           the file's own work week -> Calendar
//     core/plan bridge + CPM      reader -> PlanInput -> recalculated dates,
//                                        slack, criticality (summaries rolled up)
//     core/view RecordSet         one row per live task, keyed by PjField id
//     core/sheet SheetModel       the left-hand grid over that RecordSet
//     core/gantt GanttSource      one bar input per task, in the same row order
//
// and reports what it found in a copyable `Status`.  `recalculate()` runs the
// scheduling half again over the in-memory PlanInput -- no file is reopened --
// and refreshes the rows, the RecordSet values, the Gantt bars and the status
// counters.  `status_text()` / `dump()` are the headless, name-free renderings
// the tests print.
//
// WHO USES IT.  A macOS AppKit shell (or a future CLI) reads the sheet model and
// gantt source and draws; it never sees core/mpp, core/plan or core/cal types.
// This header deliberately includes no reader, plan or calendar-table header: a
// shell only needs date32, RecordSet, SheetModel, GanttSource and Status.
//
// WHAT IT DOES NOT DO
//   * No drawing, fonts, colours, points/DPI or windowing types.
//   * No writing/saving.  The sheet model supports editing, but this session
//     model has no write path, so an edit is an in-memory change only; nothing
//     here claims a .mpp can be saved.  (`core/mppwrite` exists; wiring it is a
//     later stream.)
//   * No globals, no exceptions escaping `load`: a failure comes back as
//     `LoadResult{nullptr, error}`.  A failed *recalculation* is not a failed
//     load: the file is still shown, with the stored dates.
//   * No clock: `Options::status_date` is kDateNa unless the caller supplies one.
//
// ROW ORDER AND IDENTITY.  Rows are in the reader's FixedData order (MS
// Project's ID order for a healthy file), and that order is shared by
// `rows()`, the RecordSet, the SheetModel and GanttSource::tasks, so
// "sheet row ordinal == gantt task index == session row index" holds.  Task
// UniqueID is the identity: `row_uid(row)` and `row_of_uid(uid)`; it is also
// carried in a session-private RecordSet field (`kUidField`) for a UID column.
//
// DATES.  `mpp::PackedDate::raw` is numerically a `core::sched::date32`: the
// file's day counter and the engine's agree (verified end to end by
// `tools/recalc --compare`, start 83/83 and finish 83/83 on PROJECT_A, which compares
// `PlanTaskResult` date32 values with the raw task-row word).  So the session
// maps a packed date by *identity*, with only the not-a-date words translated:
// 0 and 0xffffffff become kDateNa, and so does a day outside date32's usable
// range.  There is no day shift.  (app/layout_model.cpp adds 1 to the day; that
// is app-private prior art and is NOT used here -- see memory/session.md.)
//
// COMPUTED vs STORED.  When the recalculation succeeds the rows carry the
// RECALCULATED start/finish/slack/criticality -- what MS Project itself shows
// after CalculateProject -- and `Row::stored_start` / `stored_finish` keep what
// the file stored, for comparison.  When it fails (or `Options::recalculate` is
// false) the rows carry the stored values and `Status::recalculated` is false.
//
// CLIENT DATA.  `Status::title` and `Row::name` are client data when the file is
// a client programme.  `status_text()` and `dump()` deliberately omit the title
// AND every task name, so their output is safe to paste into a test log or a
// ticket; the UI gets names from the RecordSet / rows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "date.h"
#include "duration.h"
#include "gantt.h"
#include "record_set.h"
#include "sheet.h"

namespace core::session {

using core::sched::date32;
using core::sched::kDateNa;
using core::sched::ticks;

// Returned by every row lookup that misses.  Never a valid row index.
inline constexpr std::size_t kNoRow = static_cast<std::size_t>(-1);

// Session-private RecordSet field carrying a row's Task UniqueID, so a UID
// column can be shown: the descriptor table has no Task.UniqueID (UniqueID is
// RecordSet identity, not a display field -- record_set.h).  Checked against
// core/model/field_table.gen.cpp: no Task descriptor uses 0x0b4fff02.  It never
// reaches a file.  (app/layout_model.h uses 0x0b4fff01 for the same purpose; the
// two ids are distinct so a RecordSet could carry both.)
inline constexpr core::view::FieldId kUidField = 0x0b4fff02u;

// What `load()` should do.  Every field has a value-initialised default so an
// aggregate `Options{}` is fully defined (clang/GCC -Wmissing-field-initializers).
struct Options {
    // Run core/plan's CPM + rollup after reading.  False leaves the stored dates
    // in place and Status::recalculated false; recalculate() can run it later.
    bool recalculate = true;
    // Decode the file's own calendar (core/cal) and schedule on it.  When the
    // table cannot be decoded the stock Mon-Fri 08:00-12:00/13:00-17:00 week is
    // used and a warning is recorded.
    bool use_file_calendar = true;
    // Empty = the project's default calendar from 114/Props.  Only used when
    // use_file_calendar is true.
    std::string calendar_name;
    // Vertical status marker for the Gantt, and the sheet's "overdue" date.
    // kDateNa means none; this model never invents "today".
    date32 status_date = kDateNa;
    // Day columns for gantt_layout(0).  Ignored by every other part.
    double pixels_per_day = 20.0;
};

// One task row, reader order.  `start`/`finish`/`total_slack`/`critical` are the
// EFFECTIVE values (recalculated when it succeeded, stored otherwise); every
// `stored_*` member is always what the file held.  `outline_level` is 0-based
// because that is what core::sheet wants (the reader's OutlineLevel is 1-based).
struct Row {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;
    std::string outline_number;
    std::string name;
    ticks duration_ticks = 0;
    std::uint16_t percent_complete = 0;
    date32 start = kDateNa;
    date32 finish = kDateNa;
    date32 stored_start = kDateNa;
    date32 stored_finish = kDateNa;
    ticks total_slack = 0;
    ticks stored_total_slack = 0;
    int outline_level = 0;
    bool summary = false;
    bool milestone = false;
    bool critical = false;
    bool stored_critical = false;
    bool start_is_actual = false;
    bool finish_is_actual = false;
    bool stored_start_is_actual = false;
    bool stored_finish_is_actual = false;
    // Reader row +178: -1 means "use the project calendar".  Recorded so a
    // caller can tell whether per-task calendars would matter; this model still
    // schedules every task on the project calendar (Status carries the count).
    std::int32_t calendar_uid = -1;
};

// One link mapped for the Gantt (and, when endpoint-live, for the schedule).
// `critical` is a scheduler hint the reader does not expose, so it stays false
// rather than being guessed.
struct LoadedLink {
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    core::gantt::LinkType type = core::gantt::LinkType::finish_to_start;
    ticks lag = 0;
    bool critical = false;
};

// A copyable report of the load/recalculation.  Counts only: no task, calendar,
// resource or assignment name is stored in it (title is the only client-data
// field, and status_text() omits it).
struct Status {
    std::string title;  // project title; client data, status_text() omits it
    std::string path;   // as given to load()

    // Project dates: the recalculation's when it succeeded, else the reader's
    // project summary row.  `stored_project_*` are always the reader's (+96/+100
    // on the project row).
    date32 project_start = kDateNa;
    date32 project_finish = kDateNa;
    date32 stored_project_start = kDateNa;
    date32 stored_project_finish = kDateNa;

    std::size_t tasks = 0;
    std::size_t links = 0;                     // reader rows (PROJECT_A: 88)
    std::size_t links_with_live_endpoints = 0; // both uids are live tasks
    std::size_t skipped_links = 0;             // dropped from the schedule and the Gantt
    std::size_t reader_skipped_link_rows = 0;  // rows with no Task-link metadata
    std::size_t summary_links = 0;             // dropped from CPM (summary endpoint)
    std::size_t resources = 0;
    std::size_t assignments = 0;

    std::size_t summaries = 0;
    std::size_t milestones = 0;
    std::size_t critical = 0;
    std::size_t actual_starts = 0;
    std::size_t actual_finishes = 0;
    std::size_t tasks_with_calendar = 0;  // reader +178 != -1 (still project calendar)

    // The scheduling half.  `recalculated` is true only for a CpmError::none run.
    bool recalculated = false;
    std::string recalc_error;  // engine message when recalculated is false
    std::size_t recalc_tasks = 0;
    std::size_t recalc_start_matches = 0;     // recalculated == stored, per row
    std::size_t recalc_finish_matches = 0;
    std::size_t recalc_slack_matches = 0;     // total slack
    std::size_t recalc_critical_matches = 0;

    // In the order they were produced: calendar fallback, task-calendar count,
    // plan notes that change what is shown, summary-flag disagreements, reader
    // warning count.  Never contains a name from the file.
    std::vector<std::string> warnings;
};

class Session;

// `load()`'s return.  `session` is null exactly when `error` is non-empty.
struct LoadResult {
    std::unique_ptr<Session> session;
    std::string error;

    explicit operator bool() const noexcept { return session != nullptr; }
};

// Any Task field id by its descriptor name ("Start", "TotalSlack", ...); 0 when
// the name is unknown.  Exposed so a shell/test reads a RecordSet value without
// resolving names itself.
core::view::FieldId task_field_id(std::string_view field_name);

// UID / ID / Outline / Name / Start / Finish / Duration / Total Slack.  Widths
// are characters (core/view convention); SheetMetrics::char_width turns them
// into points.  UID, ID, Outline and Total Slack are read-only (identity or
// computed); the model has no write path anyway.
std::vector<core::sheet::SheetColumn> default_sheet_columns();

// row_height 20 matches default_gantt_style().row_height so the sheet and Gantt
// rows line up; header_height 32 is the two 16-point Gantt header tiers.
core::sheet::SheetMetrics default_sheet_metrics();

// GanttStyle defaults with row_height from default_sheet_metrics().
core::gantt::GanttStyle default_gantt_style();

// The loaded document.  Constructed only by load(); non-copyable and
// non-movable because the SheetModel holds a reference to the RecordSet (moving
// the Session would leave that reference dangling).
class Session {
  public:
    ~Session();

    Session(Session const&) = delete;
    Session& operator=(Session const&) = delete;

    // ---- identity ---------------------------------------------------------
    std::string const& path() const noexcept { return path_; }
    std::string const& title() const noexcept { return title_; }
    Status const& status() const noexcept { return status_; }

    // ---- the two view models ---------------------------------------------
    core::view::RecordSet& records() noexcept { return records_; }
    core::view::RecordSet const& records() const noexcept { return records_; }
    core::sheet::SheetModel& sheet() noexcept { return *sheet_; }
    core::sheet::SheetModel const& sheet() const noexcept { return *sheet_; }

    // ---- the Gantt --------------------------------------------------------
    // One TaskBarInput per row, in row order, with the file's calendar attached
    // for shading.  `gantt_layout(0)` uses Options::pixels_per_day and seeds the
    // timescale origin from the project start (else the earliest task start):
    // build_gantt only ever moves the origin earlier, so an unseeded timescale
    // would put the 1984 epoch in front of the first bar.
    core::gantt::GanttSource const& gantt_source() const noexcept { return gantt_; }
    core::gantt::GanttLayout gantt_layout(double pixels_per_day = 0.0) const;
    core::gantt::GanttStyle const& gantt_style() const noexcept { return gantt_style_; }
    core::sheet::SheetMetrics const& sheet_metrics() const noexcept { return metrics_; }
    core::sched::Calendar const& calendar() const noexcept { return calendar_; }

    // ---- rows -------------------------------------------------------------
    std::vector<Row> const& rows() const noexcept { return rows_; }
    // Empty Row for an out-of-range index (never throws).
    Row const& row(std::size_t index) const noexcept;
    std::vector<LoadedLink> const& links() const noexcept { return links_; }

    std::size_t task_count() const noexcept { return rows_.size(); }
    std::size_t link_count() const noexcept { return links_.size(); }

    // RecordSet row identity (0 when out of range; RecordSet::uid is 0 there).
    std::uint32_t row_uid(std::size_t row) const noexcept;
    // kNoRow when absent.
    std::size_t row_of_uid(std::uint32_t uid) const noexcept;

    date32 task_start(std::size_t row) const noexcept;
    date32 task_finish(std::size_t row) const noexcept;
    date32 stored_task_start(std::size_t row) const noexcept;
    date32 stored_task_finish(std::size_t row) const noexcept;
    ticks task_total_slack(std::size_t row) const noexcept;
    bool task_critical(std::size_t row) const noexcept;

    // Run core/plan::recalculate over the PlanInput captured by load() (no file
    // I/O), then refresh the rows, the RecordSet, the Gantt bar dates and the
    // status counters.  Returns Status::recalculated.  Idempotent: a second call
    // over unchanged input reports the same counters (spec §5.3).
    bool recalculate();

    // Name-free renderings for tests and logs.  status_text() is one summary
    // block; dump() adds one line per row.  Neither prints the project title or
    // a task name.
    std::string status_text() const;
    std::string dump() const;

  private:
    Session();
    friend LoadResult load(std::string const& path, Options const& options);

    // core::plan::PlanInput + PlanResult live behind this so session.h does not
    // pull core/plan (and through it core/mpp) into a shell translation unit.
    struct PlanData;

    void build_views();
    void refresh_views();
    void apply_stored_values();
    void apply_plan_result();
    void collect_status_counts();
    void rebuild_status_dates();
    void rebuild_warnings();
    void write_row_record(std::size_t index);

    core::view::RecordSet records_;
    std::unique_ptr<core::sheet::SheetModel> sheet_;
    core::gantt::GanttSource gantt_;
    core::gantt::GanttStyle gantt_style_;
    core::sheet::SheetMetrics metrics_;
    core::sched::Calendar calendar_;
    std::vector<Row> rows_;
    std::vector<LoadedLink> links_;
    Status status_;
    Options options_;
    std::string path_;
    std::string title_;
    std::unique_ptr<PlanData> plan_;
    // Raw counters the deterministic warning list is rebuilt from, so
    // recalculate() never appends a duplicate warning.
    std::size_t reader_warnings_ = 0;
    // Wave 25 (W25-11/S13): Schedule-vs-reader facts, named for what they count.
    std::size_t summary_disagreements_ = 0;    // scheduled summary != reader's Summary
    std::size_t rows_without_schedule_ = 0;    // no PlanResult row for a live row
    bool calendar_fell_back_ = false;
    // True once recalculate() has run (even if it failed), so apply_plan_result()
    // can tell "never attempted" from "attempted and produced nothing".
    bool recalc_attempted_ = false;
};

// Read `path`, decode its calendar, recalculate its schedule and build the two
// view models.  Never throws: an unreadable file, a missing table or a bad
// argument comes back as LoadResult{nullptr, error}.  A CPM error does NOT fail
// the load; it is reported in Status::recalc_error with the stored dates shown.
LoadResult load(std::string const& path, Options const& options = {});

}  // namespace core::session
