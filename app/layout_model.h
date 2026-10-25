// app/layout_model.h -- the portable half of the macOS shell.
//
// One job: map a loaded .mpp (or anything that can produce TaskRow/LinkRow) to
// the two cores the window draws from -- core::view::RecordSet for the sheet and
// core::gantt::GanttSource/GanttLayout for the Gantt -- plus the row strings the
// headless test asserts.  Nothing in this header touches AppKit, so the mapping
// builds and is unit-tested on Windows and macOS alike
// (app/layout_model_tests.cpp).
//
// WHAT IT DOES NOT DO
//   * It does not read a file.  app/reader_adapter.h is the one place that knows
//     about mpp::Document; this header only knows date32, mpp::CustomFieldValue
//     (a plain data struct from core/mpp/field_value.h, no reader) and
//     RecordSet, which keeps the mapping testable with synthetic rows.
//   * It does not draw and it does not choose fonts or colours.
//   * It does not schedule, roll up or recompute.  Start/Finish/Summary/
//     Milestone/Critical are the reader's values (documented derivations in
//     core/mpp/reader.h); the Gantt only lays them out.
//
// DATE ENCODINGS -- two names for the same counter, so the conversion is IDENTITY
//   mpp::PackedDate (the file): (day << 16) | ticks_of_day, day 0 = 1983-12-31,
//   one tick = 6 seconds.  core::sched::date32 (the engine): day 1 = 1984-01-01,
//   one unit = 1/10 minute = 6 seconds.  Both count days from 1983-12-31 (day 1 =
//   1984-01-01 in each), so packed day D and date32 day D are the same date and
//   the units are the same size: date32 = (day << 16) | ticks, exactly the file
//   word.  date32_from_packed()/packed_from_date32() are that identity, and they
//   are the only place the conversion is written.
//
//   W38 CORRECTION: this file used to add 1 to the day ("day32 = packed_day +
//   +1"), which displayed every date one day late.  Evidence: spec §3 pins
//   packed 15346 = 2026-01-05 and 15636 = 2026-10-22, `core/sched/date.h` pins
//   day 1 = 1984-01-01, and `weekday_of_day(0) = 6` (1983-12-31 was a Saturday).
//   The round-trip test and a real task's stored start (a Monday, not a Tuesday)
//   now pin the identity.
//
// UNITS
//   RecordSet duration values are 6-second ticks (core::view::kTicksPerDay =
//   4800 for an 8-hour day).  Gantt percent_complete is 0..1, not 0..100.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "date.h"
#include "field_value.h"  // mpp::CustomFieldValue: plain data, no reader
#include "gantt.h"
#include "gantt_style.h"
#include "record_set.h"
#include "resource_tables.h"  // core::view::ResourceType for ResourceRow
#include "sheet.h"

namespace app {

using core::sched::date32;
using core::sched::kDateNa;

// A RecordSet field id used only by this app to carry a row's UniqueID so the
// sheet can show a UID column: the descriptor table has no Task.UniqueID field
// (UniqueID is RecordSet identity, not a display field -- see record_set.h).
// The id is checked against core/model/field_table.gen.cpp: unused there, past
// every real Task id, and it never goes near the file or the writer.
inline constexpr core::view::FieldId kUidField = 0x0b4fff01u;

// ---------------------------------------------------------------------------
// The reader-independent plan model
// ---------------------------------------------------------------------------

// One task row.  `outline_level` is 0-based (0 = top level) because that is what
// core::sheet::SheetModel::outline_level() returns; the reader's Project
// OutlineLevel (1-based) is decremented in reader_adapter.h.
struct TaskRow {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;
    /* Wave 24 (W24-1): the task's row number in the FILE's FixedData storage
     * order (core/mpp Task::row: 0-based, INCLUDING the project row, so real
     * tasks start at 1).  The adapter sorts `tasks` into Project's ID order,
     * so vector position is no longer the stored order; the Insert Task
     * placeholder must copy the LAST live stored row's dates (what
     * core/mppwrite's add_task_row clones), which is the row with the largest
     * file_row.  A pending insert has no file row (0, never a real task). */
    std::uint32_t file_row = 0;
    std::string outline_number;  // "1", "1.1" -- Project's Task.OutlineNumber
    std::string name;
    std::int64_t duration_ticks = 0;
    std::uint16_t percent_complete = 0;  // 0..100
    date32 start = kDateNa;
    date32 finish = kDateNa;
    int outline_level = 0;
    bool summary = false;
    bool milestone = false;
    bool critical = false;
    // Wave 23 (W23-6): Task.Flag10 (0x0b400051), Project's SUMMARY column, a
    // stored Task FixedMeta bit (reader.h Task::flag10).  `stored` is
    // false for a row the file's metadata does not cover; the record then has
    // no value (Project's No is a stored False, not an absent field).
    bool flag10 = false;
    bool flag10_stored = false;
    // Wave 14: the reader's MEASURED manually-scheduled bit (Fixed2Meta 0x80;
    // core/mpp/reader.h Task::manually_scheduled).  A pending inserted row has no
    // file row and is false.  app/start_edit_warning.h uses it to warn that a date
    // save cannot keep a manual task manual.
    bool manually_scheduled = false;
    // Wave 12: the stored custom-field values the reader decoded (W48's measured
    // encodings; see core/mpp/field_value.h).  Only values the file actually
    // stores are listed, plus Flag1 for every live row (a clear bit is a stored
    // False).  Empty for a producer that has none and for a pending inserted row.
    std::vector<mpp::CustomFieldValue> custom_fields;

    // The stored value of one custom field, or nullptr when the row does not
    // carry it.  A null pointer is "not stored"; a present value with
    // `storage_measured == false` is "stored but not decodable".
    const mpp::CustomFieldValue* custom_field(std::uint32_t field_id) const noexcept {
        for (const mpp::CustomFieldValue& field : custom_fields) {
            if (field.field_id == field_id) {
                return &field;
            }
        }
        return nullptr;
    }
};

struct LinkRow {
    // Wave 13: the file's link row uid (0 for a link a producer cannot name, and
    // for a pending add the shell is only displaying).  The Predecessors dialog
    // deletes by uid because (predecessor, successor, type) is what the writer
    // treats as the relation's identity; the uid is what addresses one row.
    std::uint32_t uid = 0;
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    core::gantt::LinkType type = core::gantt::LinkType::finish_to_start;
    // The stored lag in 6-second file ticks (4800 = one 8-hour working day,
    // negative = lead).  The Predecessors dialog shows and edits it.
    std::int32_t lag_ticks = 0;
    // Wave 23 (W23-7): the stored PjFormatUnit (link row +18): 7 = working
    // days, 8 = elapsed days, 5/6 hours, 9/10 weeks, 11/12 months, 19 percent.
    // Project prints the lag with this unit's label, not the sheet's
    // magnitude-chosen one.
    std::uint16_t lag_units = 0;
    bool critical = false;
};

// One resource, as the reader exposes it (wave 11).  `type` is Project's
// PjResourceTypes order (0 Work, 1 Material, 2 Cost) -- the reader reports Work
// because no corpus file proves the other encodings, and the field exists here
// so a later reader change needs no model change.  `max_units_percent` is the
// view convention (100.0 == 100 %), the same one core/view/resource_tables.h
// documents for its builder.
struct ResourceRow {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;
    std::string name;      // display form; an in-memory value, never written out
    std::string initials;
    core::view::ResourceType type = core::view::ResourceType::work;
    double max_units_percent = 100.0;
};

// One assignment, as the reader exposes it (wave 11).  Work is carried in
// 6-second ticks (the file unit); the usage model wants minutes and divides by
// core::view::kTicksPerMinute when it builds its input (app/view_model.h).
// `units_percent` is 100.0 == 100 %, matching ResourceRow.
struct AssignmentRow {
    std::uint32_t uid = 0;
    std::uint32_t task_uid = 0;
    std::uint32_t resource_uid = 0;  // 0 = no resource
    std::int64_t work_ticks = 0;
    double units_percent = 100.0;
    date32 start = kDateNa;
    date32 finish = kDateNa;
};

struct PlanInput {
    std::string title;
    std::string source_name;  // file name only; diagnostics, never a committed value
    std::vector<TaskRow> tasks;
    std::vector<LinkRow> links;
    // Wave 11: resource and assignment rows for the resource sheet and the usage
    // views.  Both are empty for a producer that has none (the task views and
    // the Gantt do not read them).
    std::vector<ResourceRow> resources;
    std::vector<AssignmentRow> assignments;
    date32 project_start = kDateNa;
    date32 project_finish = kDateNa;
    /* Wave 26 (W26-5.1): the project row's Priority word (FixedData +78 of row
     * 0), the one Project Information field this port can both read and write.
     * Project's new-project value is 500; a producer that does not store the
     * word leaves the reader's 0 rather than this default, so the value shown is
     * the file's. */
    std::uint16_t project_priority = 500;
    std::size_t skipped_links = 0;  // links whose endpoint the reader did not know

    /* Wave 23 (B-1) / wave 24 (W24-2): Project's project-summary row, shown as
     * ID 0 above the top-level tasks.  The values come from the file's project
     * row.
     *
     * `show_project_summary` is the FILE's stored setting (214/212/Props record
     * 0x03400009: 1 shown, 0 hidden), decoded by the reader.  A file without
     * the record is treated as Project's new-project default (hidden).  Wave
     * 23 wrongly hard-coded this on after a props diff that did not decode the
     * record framing; wave 24 re-measured a fresh Project save pair and the
     * record goes 0 -> 1 exactly with the setting (see ProjectRow in
     * core/mpp/reader.h).  The shell has no Format-tab toggle yet, so a file
     * that says hidden stays hidden. */
    bool show_project_summary = false;
    std::int64_t project_duration_ticks = 0;
    std::uint16_t project_percent_complete = 0;

    /* Wave 24 (W24-3): the Timeline strip's visibility, from the file's view
     * props (214/212/Props record 0x03400008: "Time&line" shown, empty hidden).
     * Measured on a shown/hidden save pair from one session: every semantic
     * stream difference was that record (plus the per-save GUID), and two
     * fresh Project sessions reopened the pair shown and hidden.  A file with
     * no record keeps Project's new-file default (shown). */
    bool show_timeline = true;

    /* Wave 24 (W24-9/R11): the project row's own SUMMARY value -- the stored
     * FixedMeta Flag10 bit of row 0.  Wave 23 hard-coded Yes from one capture;
     * the reader now decodes the file's bit and the sheet shows what it says
     * (a file without a usable entry leaves the cell blank). */
    bool project_flag10 = false;
    bool project_flag10_stored = false;

    /* Wave 28 (W28-1.3): the file's project STATUS DATE (Project Information's
     * Status date), the date Mark on Track measures progress through.  The
     * reader decodes the stored 114/Props record 0x02400045 (reader.h
     * ProjectRow::status_date has the measured evidence).  `stored` is false
     * when the file carries no such record; `status_date` is then kDateNa and
     * must not be treated as "today" or as a default.  Update Project writes
     * its own through date into this record; Mark on Track never moves it. */
    date32 status_date = kDateNa;
    bool status_date_stored = false;
};

// The synthetic TaskRow make_record_set() puts at row 0 when
// `input.show_project_summary` is set.  Its outline_level is -1: the project
// row and Project's top-level tasks share the same text indent in the clean
// capture, while the row is still the parent whose collapse hides everything.
// uid and id are 0 in Project; `summary` is true and `critical` false.
TaskRow project_summary_row(const PlanInput& input);

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

// mpp::PackedDate's raw u32 -> date32.  0 and 0xffffffff are "not set" in the
// file (mpp::PackedDate::valid()), so they become kDateNa.  A day at or past the
// top of date32's 16-bit day field also becomes kDateNa rather than wrapping.
date32 date32_from_packed(std::uint32_t packed_raw) noexcept;

// date32 -> the file form, for round-trip tests (and for a future writer).
std::uint32_t packed_from_date32(date32 day) noexcept;

date32 date32_from_date_value(const core::view::DateValue& value) noexcept;
core::view::DateValue date_value_from_date32(date32 day) noexcept;

// Today's civil date (UTC; the status line's default in the app, since reader
// v0 exposes no status date).  kDateNa only if the system clock is unreadable.
date32 today_date32();

// ---------------------------------------------------------------------------
// Sheet mapping
// ---------------------------------------------------------------------------

// The exact text the sheet shows for one row.  The app draws through
// core::sheet::SheetModel (which formats the same values), so this is both the
// documented mapping and the headless test's independent expectation; the test
// cross-checks the two.
struct SheetRowText {
    std::string uid;
    std::string id;
    std::string outline;
    std::string name;
    std::string start;
    std::string finish;
    std::string duration;
};

SheetRowText sheet_row_text(const TaskRow& row, const core::sheet::SheetFormat& format);

// UID / ID / Outline / Name / Start / Finish / Duration.  Widths are characters
// (core/view convention); SheetMetrics::char_width turns them into points.
std::vector<core::sheet::SheetColumn> default_sheet_columns();

// row_height 20 matches default_gantt_style().row_height so the sheet and Gantt
// rows line up; header_height 32 is the two 16-point Gantt header tiers.
core::sheet::SheetMetrics default_sheet_metrics();

// Resolved Task.Name field id (0 if the descriptor table ever loses the name).
core::view::FieldId name_field_id();

// Any Task field id by its descriptor name ("OutlineLevel", "Duration", ...);
// 0 when the name is unknown.  Exposed so the callers (and the test) can read a
// field from the RecordSet without re-resolving names themselves.
core::view::FieldId task_field_id(std::string_view field_name);

// Fields: UID (kUidField), ID, OutlineNumber, Name, Start, Finish, Duration,
// PercentComplete, Summary, Milestone, Critical, OutlineLevel (0-based), plus
// every stored custom field the task rows carry (wave 12).  A custom field is
// added to the schema even when no row stores it, so a column never disappears
// because a value is missing.
core::view::RecordSet make_record_set(const PlanInput& input);

// The RecordSet value of one stored custom field: Text/Number/Date/Duration/Flag
// map to the matching core::view kind, and a value the file stores but the reader
// could not decode (or a Cost) becomes Null rather than a guess.  Declared here
// (not only used inside make_record_set) so the stored-column tests can pin the
// conversion without a RecordSet round trip.
core::view::Value value_of_custom_field(const mpp::CustomFieldValue& field);

// The reverse mapping: reads the editable task fields back out of `records` into
// a copy of `base` (uid, name, start, finish, duration, percent complete,
// summary, milestone, critical, outline level).  `base` supplies what the
// RecordSet does not carry: the title, the project dates, the links and the
// skipped-link count.
//
// WHY. Once the user edits a cell, the RecordSet -- not the loaded plan -- is
// the source of truth for that value (core::sheet writes through to it, and undo
// writes back), so the app re-derives the plan it draws from before every
// rebuild.  A start the user cleared reads back as kDateNa, which is exactly
// what an undated task should be: the sheet keeps the row and the Gantt gives it
// no bar.
PlanInput sync_plan_from_records(const PlanInput& base, const core::view::RecordSet& records);

// The columns the shell declares read-only: UID, ID and Outline number are
// identity or calculated columns (RecordSet identity, Project's own ID column,
// the outline path), so core::sheet refuses begin_edit on them while Name,
// Start, Finish and Duration stay editable.  core/sheet deliberately does not
// infer this ("read-only is host configuration"), so the policy lives here and
// the headless test asserts it.
void apply_edit_policy(core::sheet::SheetModel& sheet);

// Wave 25 (W25-1/S1, the R5 guard): the one edit-entry decision every shell
// uses for a displayed sheet row.  A uid-0 row is the synthetic project-summary
// row: it has no file row, and sync_plan_from_records deliberately skips it, so
// an edit there would be accepted and then silently dropped -- refuse it before
// a session opens.  Any other row goes through core::sheet, which refuses a
// declared read-only column.  `error` is the status message on a refusal.
bool begin_cell_edit(core::sheet::SheetModel& sheet, std::size_t row, std::size_t column,
                     std::string& error);

// ---------------------------------------------------------------------------
// Gantt mapping
// ---------------------------------------------------------------------------

// One Gantt row per PlanInput task, in the same order, for a caller that draws
// every task (print, the whole-plan chart).  It carries no project row: a
// displayed view must use make_gantt_source_for_displayed_rows() instead, which
// resolves each RecordSet row through its uid.  `holidays` is extra non-working
// shading on top of Saturday/Sunday; it never moves a bar.  `status_date` draws
// the status marker (kDateNa for none).
core::gantt::GanttSource make_gantt_source(const PlanInput& input,
                                           const std::vector<date32>& holidays = {},
                                           date32 status_date = kDateNa);

// As make_gantt_source(), but only the DISPLAYED rows get a Gantt row, in that
// order.  `rows` are RecordSet row indexes (SheetModel::visible_rows() /
// app::RowMap::rows), and every one is resolved through its uid in `records`:
// uid 0 is the synthetic project row (project_summary_row()), any other uid is
// looked up in input.tasks.  A row index outside the RecordSet, or a uid with
// no task, gets no bar.
//
// WHY BY UID (wave 25, W25-1/S1).  The RecordSet may carry the project row
// ahead of the tasks (a file whose 0x03400009 is set), so RecordSet row N is
// task N only when that row is hidden.  Indexing input.tasks with a RecordSet
// row shifts every bar and drops the last one -- on the macOS screen Gantt and
// the smoke sequence, which both see the adapter's row now.
//
// After a collapse Gantt row N is still visible ordinal N, a hidden task has no
// bar, and a link whose endpoint is hidden is dropped by build_gantt() (it skips
// a link with no bar).  This is the whole of "the Gantt follows the visible row
// list": the chart is rebuilt, never panned or blanked.
core::gantt::GanttSource make_gantt_source_for_displayed_rows(
    const PlanInput& input, const core::view::RecordSet& records,
    const std::vector<std::size_t>& rows, const std::vector<date32>& holidays = {},
    date32 status_date = kDateNa);

// Day columns at `pixels_per_day`, Monday-first weeks, origin replaced by
// build_gantt()'s backward snap.
core::gantt::Timescale default_timescale(double pixels_per_day);

// The timescale the window should use: default_timescale() with the origin
// seeded from the project start (else the earliest task start).  It matters:
// build_gantt() only ever moves the origin EARLIER, so an unseeded timescale
// would keep the 1984 default origin and leave thousands of empty day columns
// before the first bar (gantt.h: "the caller sets origin to the project start").
core::gantt::Timescale timescale_for_plan(const PlanInput& input, double pixels_per_day);

// The same seeding, for one of the three zoom levels the View menu offers
// (core/gantt's own zoom_spec() names the unit and the pixels per unit, so the
// shell does not invent a fourth scale).  This is what the timescale preference
// in app/prefs.h applies through.
core::gantt::Timescale timescale_for_zoom(const PlanInput& input, core::gantt::GanttZoom zoom);

// Wave 29 (W29-1.4): the applied view's saved timescale scroll origin and
// zoom, as PROJECT_A stores them.  The two stored fields are
//   +1176 u32  the saved left edge, tenths of a minute since 1983-12-31 00:00;
//   +1180 u16  the saved size percentage (MPXJ reads only its low byte).
// The reference values and how the window maps the percentage to pixels are in
// tests/data/reference/w29-projecta-timescale.measures.json: the applied view
// stores 2025-12-01 and 262 with a months bottom tier, and Project 2021's clean
// capture renders the same day width the mapping below produces (3.287 px/day).
struct StoredTimescale {
    bool origin = false;  // `scale.origin` and `pin_origin` were set
    bool zoom = false;    // `scale.unit` and `pixels_per_unit` were set
};

// +1176 u32 -> date32 (00:00 of the stored instant); kDateNa for 0, which is
// what a view with no saved timescale stores.  14400 units is one day and one
// date32 day, so the conversion is a division of the same counter.
core::sched::date32 stored_timescale_origin(std::uint32_t stored) noexcept;

// Applies whichever of the two stored fields is usable, in place:
//   * origin: always when +1176 is non-zero, with pin_origin set so
//     build_gantt() does not move it back over an earlier task;
//   * zoom: when +1180 is positive and the bottom tier is a measured unit
//     (days: 24 px column at 100%; months: 38.2 px column at 100%).  The
//     result is a calendar-day axis at base * size/100, which is what the
//     capture's proportional month columns and 3.287 px/day show.
// An unmeasured bottom unit leaves the zoom half alone (origin may still apply).
StoredTimescale apply_stored_timescale(const msproject::viewstate::GanttStyle& style,
                                       core::gantt::Timescale& scale);

// GanttStyle defaults with row_height taken from default_sheet_metrics() so the
// two halves of the window stay aligned.  The app sets measure_text to a real
// font metric; the model falls back to 7 px per code point.
core::gantt::GanttStyle default_gantt_style();

// ---------------------------------------------------------------------------
// Summary (the app's status line, and the test's report)
// ---------------------------------------------------------------------------

struct PlanSummary {
    std::size_t tasks = 0;
    std::size_t links = 0;
    std::size_t summaries = 0;
    std::size_t milestones = 0;
    std::size_t critical = 0;
    date32 first_start = kDateNa;
    date32 last_finish = kDateNa;
};

PlanSummary summarize(const PlanInput& input);

}  // namespace app
