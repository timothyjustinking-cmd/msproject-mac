/*
 * report.h -- a printable sheet report and Gantt report, drawn with PdfWriter.
 *
 * The two entry points consume the SAME models the app draws on screen, so a
 * report cannot disagree with the view:
 *
 *   render_sheet_report(pdf, request)
 *       core::view::RecordSet + a column list -> core::sheet::SheetModel, and
 *       every cell's text comes from SheetModel::cell_text().  Date, duration,
 *       percent, cost, bool and outline formatting are therefore the sheet's,
 *       not a second implementation.  Collapsed summary rows are respected
 *       (visible_rows()), summary rows are bold, critical/overdue cells are red
 *       -- the same CellStyle the shell reads.
 *
 *   render_gantt_report(pdf, request)
 *       the same sheet on the left, and on the right the bars, milestones,
 *       summary bars, dependency polylines and non-working shading that
 *       core::gantt::build_gantt() computes.  Date-to-x is the Timescale's;
 *       this file never computes a date position itself.
 *
 * Both paginate: a repeated title block, a repeated column header, page numbers
 * ("Page i of n") and a legend on every page.  The return value is the number of
 * pages produced (always >= 1, even for an empty project).
 *
 * COORDINATES AND UNITS
 *   Everything is points, y down from the top-left of the page, exactly like
 *   PdfWriter and the two models.  A4 landscape is the default (842 x 595).
 *
 * DETERMINISM
 *   No clock: /Info /CreationDate comes from PdfInfo, whose default is fixed.
 *   The same request always produces the same bytes.  The request's `info` is
 *   applied to the writer only when `info.title` is non-empty, so a caller that
 *   already set document info keeps it.
 *
 * WHAT IS A DECISION HERE (no oracle evidence)
 *   Margins, row height, font sizes, legend text, column-width policy and the
 *   timescale unit thresholds are presentation choices made in this file.  The
 *   DATA (cell text, bar geometry, link routing, shading) is the models'.
 */

#ifndef CORE_RENDER_REPORT_H
#define CORE_RENDER_REPORT_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "gantt.h"
#include "pdf_writer.h"
#include "record_set.h"
#include "sheet.h"

namespace core::render {

/*
 * Presentation knobs.  The defaults produce an A4-landscape report with a
 * 24 pt margin, 13 pt rows, 8 pt body text and a 14 pt title.  All values are
 * points except cell_padding (also points) and the row counts, which are whole.
 */
struct ReportStyle {
    PdfPageFormat page = PdfPageFormat::a4_landscape();
    double margin = 24.0;
    double title_size = 14.0;
    double subtitle_size = 8.0;
    double header_size = 8.0;
    double cell_size = 8.0;
    double footer_size = 7.0;
    double legend_size = 6.5;
    double row_height = 13.0;
    double header_height = 16.0;
    double cell_padding = 3.0;
    double title_gap = 5.0;
    double footer_height = 24.0;
    bool show_grid = true;
    bool show_legend = true;

    PdfRgb text_color{0.10, 0.10, 0.10};
    PdfRgb muted_color{0.45, 0.45, 0.45};
    PdfRgb rule_color{0.72, 0.72, 0.72};
    PdfRgb grid_color{0.86, 0.86, 0.86};
    PdfRgb header_fill{0.91, 0.91, 0.93};
    PdfRgb critical_color{0.72, 0.10, 0.10};
    PdfRgb bar_color{0.25, 0.38, 0.70};
    PdfRgb bar_done_color{0.16, 0.26, 0.52};
    PdfRgb summary_color{0.16, 0.16, 0.16};
    PdfRgb milestone_color{0.12, 0.12, 0.12};
    PdfRgb link_color{0.40, 0.40, 0.40};

    bool valid() const noexcept {
        return page.valid() && margin >= 0.0 && row_height > 0.0 && header_height > 0.0 &&
               cell_size > 0.0 && title_size > 0.0;
    }
};

/* Fields both requests share. */
struct ReportRequestBase {
    /* The data.  Non-null and outliving the call; SheetModel holds a reference. */
    core::view::RecordSet* records = nullptr;
    /* Columns to show.  Empty means task_report_columns(). */
    std::vector<core::sheet::SheetColumn> columns;
    /* Value formatting (date order, unit labels, decimals).  Defaults are the
     * sheet's: "3 days", "Tue 20/10/26", "50%", "Yes"/"No". */
    core::sheet::SheetFormat format{};
    /* char_width / indent / row height hints.  The report uses row_height from
     * the style, not from here; char_width only caps authored column widths. */
    core::sheet::SheetMetrics metrics{};
    std::string title;
    std::string subtitle;
    std::string footer;
    ReportStyle style{};
    PdfInfo info{};
};

struct SheetReportRequest : ReportRequestBase {};

struct GanttReportRequest : ReportRequestBase {
    /* Optional dependency lines; uids must match RecordSet uids. */
    std::vector<core::gantt::LinkInput> links;
    /* Unit and week anchor.  With `auto_timescale` the unit is chosen from the
     * project span and pixels_per_unit is computed to fit the chart width; the
     * origin is always snapped by build_gantt(). */
    core::gantt::Timescale timescale{};
    core::gantt::GanttStyle gantt_style{};
    /* Real working-time shading (core::cal output).  Null means the legacy
     * Monday-Friday mask plus `gantt_source_nonworking_days`. */
    std::shared_ptr<const core::sched::Calendar> calendar;
    std::vector<core::sched::date32> nonworking_days;
    bool auto_timescale = true;
    /* Width of the left table in points, before clamping to the page. */
    double table_width = 250.0;
};

/*
 * The default sheet columns: ID, Name, Duration, Start, Finish, Slack, Critical.
 * A field the descriptor table does not name is skipped, so this works with any
 * RecordSet and never invents an id.  Rows: 5, 40, 8, 14, 14, 8, 8 characters.
 */
std::vector<core::sheet::SheetColumn> task_report_columns();

/* Render the table.  Returns the page count (>= 1). */
std::size_t render_sheet_report(PdfWriter& pdf, const SheetReportRequest& request);

/* Render the table (left) plus the Gantt chart (right).  Returns page count. */
std::size_t render_gantt_report(PdfWriter& pdf, const GanttReportRequest& request);

}  // namespace core::render

#endif  // CORE_RENDER_REPORT_H
