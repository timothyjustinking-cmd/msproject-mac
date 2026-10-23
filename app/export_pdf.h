// app/export_pdf.h -- Export PDF through core/render (stream W39), driven by the
// same models the window draws.
//
// WHAT THE REPORT IS
//   `core::render::render_sheet_report()` and `render_gantt_report()` already
//   build their own core::sheet::SheetModel from a RecordSet and a column list,
//   so a report is exactly "the models the shell shows, on paper".  This file is
//   the shell's half, all portable and headless-tested:
//
//     * which ROWS: the report prints the rows the window is showing.  When a
//       filter is active the shell passes the matching RecordSet rows, so the
//       report is filtered exactly like the view (we do not re-implement the
//       filter for print).  No rows passed means every row.
//     * which FORMATS: request.format is the sheet's SheetFormat after
//       apply_preferences(), so dates print in the user's chosen order.  A
//       duration preference of hours/days is applied by substituting the
//       Duration column for an app-private Text column the shell fills with
//       app::format_duration() -- core/render keeps its own formatter, so the
//       only way to make print and screen agree is to hand it the same text.
//     * SELECTION: core/render has no selection concept (and this wave may not
//       edit core), so the selected task is named in the report footer rather
//       than highlighted.  That is documented in app/README.md; it is not a
//       claim that the printed row is marked.
//     * WORKING-DAY SHADING: the Gantt report always shades non-working time
//       from the layout.  When the preference is "off" this file hands the
//       report an all-working calendar (every day 00:00-24:00, so there is no
//       non-working span to draw); shading spans never move a bar, so nothing
//       else in the report changes.  The week anchor is pinned so the all-working
//       calendar cannot move the week columns.
//
// DETERMINISM
//   core/render is byte-deterministic (no clock), so the same request produces
//   the same PDF.  `PdfInfo::title` is the only document-info field this file
//   sets.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "calendar.h"
#include "gantt.h"
#include "layout_model.h"
#include "page_layout.h"
#include "prefs.h"
#include "record_set.h"
#include "pdf_writer.h"
#include "report.h"
#include "sheet.h"

namespace app {

enum class ReportKind : std::uint8_t {
    sheet = 0,
    gantt,
};

// A RecordSet field used only by this app to carry preformatted duration text
// into a report column.  The id is app-private (like app::kUidField): it is past
// every real Task field, it is never written to a file, and it exists only in
// the report's private copy of the RecordSet.
inline constexpr core::view::FieldId kDurationTextField = 0x0b4fff02u;

struct ExportRequest {
    ReportKind kind = ReportKind::sheet;
    // Every row; `rows` selects the subset to print.
    const core::view::RecordSet* records = nullptr;
    // RecordSet rows to print, ascending.  Empty means every row.
    std::vector<std::size_t> rows;
    // Empty means core::render::task_report_columns().
    std::vector<core::sheet::SheetColumn> columns;
    core::sheet::SheetFormat format{};
    core::sheet::SheetMetrics metrics{};
    // Wave 14: the page geometry the report is laid out on.  The SAME PageSpec
    // drives printing (app/page_layout.h build_print_job), so Export PDF and Print
    // cannot drift.  The defaults are core/render::ReportStyle's own, so the PDF
    // bytes are unchanged for every existing request.
    PageSpec page{};
    Preferences preferences{};
    std::string title;
    std::string subtitle;
    std::string footer;
    // Gantt only.
    std::vector<core::gantt::LinkInput> links;
    core::gantt::Timescale timescale{};
    bool auto_timescale = true;
    bool working_day_shading = true;
};

struct ExportOutcome {
    bool ok = false;
    std::size_t pages = 0;
    std::size_t rows = 0;    // rows actually printed
    std::string path;
    std::string error;       // non-empty exactly when ok == false
};

// Writes the report to `path`.  Failure (no records, an empty/unwritable path,
// a renderer or writer error) is reported, never thrown.
ExportOutcome export_report(const ExportRequest& request, const std::string& path);

// The calendar a report should be given: nullptr to let the layout shade from
// its weekend mask, or an all-working calendar when the shading preference is
// off (see the header comment).
std::shared_ptr<const core::sched::Calendar> shading_calendar(bool working_day_shading);

// "<kind> report - <shown> of <total> task(s)", plus the filter when one is
// active.  Used as the report's subtitle and in the status line.
std::string export_subtitle(ReportKind kind, const std::string& filter_description,
                            std::size_t shown, std::size_t total);

// Wave 25 (W25-11/S14): how many of `rows` are TASKS.  The synthetic
// project-summary row (uid 0) is a displayed row, not a task, and both Print
// and the PDF subtitle say "N of N tasks" -- so the PDF must not count it.
// An empty `rows` means every RecordSet row.
std::size_t task_row_count(const core::view::RecordSet& records,
                           const std::vector<std::size_t>& rows);

// "Selected: <text>" (when a task is selected) + the preference summary.
std::string export_footer(const Preferences& preferences, const std::string& selection_text);

// The name the save panel suggests: "plan.mpp" -> "plan-sheet.pdf" /
// "plan-gantt.pdf" (never empty).
std::string export_default_name(const std::string& source_path, ReportKind kind);

}  // namespace app
