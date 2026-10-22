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
//     about mpp::Document; this header only knows date32 and RecordSet.  That
//     keeps the mapping testable with synthetic rows and keeps core/mpp's types
//     out of the model.
//   * It does not draw and it does not choose fonts or colours.
//   * It does not schedule, roll up or recompute.  Start/Finish/Summary/
//     Milestone/Critical are the reader's values (documented derivations in
//     core/mpp/reader.h); the Gantt only lays them out.
//
// DATE ENCODINGS -- two, and the one conversion between them
//   mpp::PackedDate (the file): (day << 16) | ticks_of_day, day 0 = 1983-12-31,
//   one tick = 6 seconds.  core::sched::date32 (the engine): day 1 = 1984-01-01,
//   one unit = 1/10 minute = 6 seconds.  So day32 = packed_day + 1 and
//   tenths_of_minute = ticks_of_day -- the same six-second resolution, shifted by
//   one day.  date32_from_packed() is that conversion, and it is the only place
//   the shift is written.
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
#include "gantt.h"
#include "record_set.h"
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
};

struct LinkRow {
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    core::gantt::LinkType type = core::gantt::LinkType::finish_to_start;
    bool critical = false;
};

struct PlanInput {
    std::string title;
    std::string source_name;  // file name only; diagnostics, never a committed value
    std::vector<TaskRow> tasks;
    std::vector<LinkRow> links;
    date32 project_start = kDateNa;
    date32 project_finish = kDateNa;
    std::size_t skipped_links = 0;  // links whose endpoint the reader did not know
};

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
// PercentComplete, Summary, Milestone, Critical, OutlineLevel (0-based).
core::view::RecordSet make_record_set(const PlanInput& input);

// ---------------------------------------------------------------------------
// Gantt mapping
// ---------------------------------------------------------------------------

// One Gantt row per PlanInput task, in the same order, so
// GanttSource::tasks index == RecordSet row index == sheet row.  `holidays` is
// extra non-working shading on top of Saturday/Sunday; it never moves a bar.
// `status_date` draws the status marker (kDateNa for none).
core::gantt::GanttSource make_gantt_source(const PlanInput& input,
                                           const std::vector<date32>& holidays = {},
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
