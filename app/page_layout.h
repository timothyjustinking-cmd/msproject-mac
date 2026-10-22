// app/page_layout.h -- the printed page, as arithmetic: page size, margins,
// the vertical bands of a page, how many rows fit on one, and the header/footer
// strings.  Plain C++ with no AppKit and no PDF writer, so both the print path
// (NSPrintOperation -> app/main.mm) and the PDF path (File > Export PDF ->
// core/render) can be driven from it and a headless test can pin the numbers.
//
// WHY THIS FILE EXISTS
//   Printing and "Save as PDF" used to be two unrelated layouts: core/render
//   paginates for Export PDF, and an NSPrintOperation would paginate however
//   AppKit felt like it.  Wave 14 adds printing, and the brief's rule is that
//   the two paths must not drift.  So the page geometry lives here once:
//
//     * app/export_pdf.cpp builds core/render::ReportStyle FROM PageSpec
//       (same page size, margin, row height, header/footer bands, title sizes),
//       so core/render breaks pages at exactly page_layout().rows_per_page;
//     * app/main.mm builds an app::PrintJob (build_print_job) and draws each
//       page at the bands this file computes;
//     * app/page_layout_tests.cpp asserts the arithmetic for a 3-row and an
//       83-row plan, the header/footer text, and that the two routes produce
//       the SAME PrintJob for the same request -- the seam test.
//
// THE ARITHMETIC IS core/render's
//   core/render/report.cpp plan_page() has computed these bands since wave 10
//   (title baseline, subtitle baseline, two rules, body top/bottom, rows per
//   page).  This file mirrors that formula with the same constants
//   (margin 24, title 14, subtitle 8, title gap 5, 4 pt padding around the
//   rules, footer 24, row height 13) so the two renderers agree.  If core/render
//   ever changes, this file must change with it and the export test's page-count
//   cross-check in page_layout_tests.cpp will fail -- that is the point.
//
//   The Gantt report's body header is TALLER than the sheet's: core/render draws
//   a two-tier timescale band, kGanttHeaderHeight = 26 pt (report.cpp), while the
//   sheet's column header is style.header_height = 16 pt.  That is why
//   PrintKind changes rows_per_page: 83 rows is 3 pages either way, but the page
//   break moves (36 sheet rows vs 35 Gantt rows).  core/render is the source of
//   the 26; it is not a number the app picked.
//
// HEADER / FOOTER
//   Header: "<project> - <report title>" (the project alone, or the title alone,
//   when the other is empty).  Footer: "<date>  |  Page <i> of <n>" (the page
//   part alone when the date is empty).  Both are pure string functions so the
//   tests can pin them exactly; the date text is supplied by the caller (this
//   file never reads a clock, which is what keeps a print job deterministic).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace app {

// Which report is being laid out.  The only difference is the height of the
// band above the first row: sheet = its one-line column header, Gantt = the
// two-tier timescale band (see the header comment).
enum class PrintKind : std::uint8_t {
    sheet = 0,
    gantt,
};

// The two callers of the same layout: a real printer, or a PDF file written by
// the print operation's save job.  The tag is carried on the job so the seam
// test can prove both routes ran through build_print_job() with the SAME pages.
enum class PrintRoute : std::uint8_t {
    printer = 0,
    pdf,
};

// Page geometry in points, y down from the top-left (the PDF writer's and
// core/render's convention).  The defaults are A4 landscape with a 24 pt margin,
// exactly core/render::ReportStyle's defaults, so exporting with a default
// ExportRequest produces byte-identical output to the pre-wave-14 path.
struct PageSpec {
    double width = 842.0;   // A4 landscape, points
    double height = 595.0;
    double margin = 24.0;
    double row_height = 13.0;
    double header_height = 16.0;        // sheet column header
    double gantt_header_height = 26.0;  // core/render kGanttHeaderHeight
    double footer_height = 24.0;
    double title_size = 14.0;
    double subtitle_size = 8.0;
    double title_gap = 5.0;

    // The width actually available to the table/chart.
    double printable_width() const noexcept { return width - 2.0 * margin; }

    bool valid() const noexcept {
        return width > 0.0 && height > 0.0 && margin >= 0.0 && width > 2.0 * margin &&
               row_height > 0.0 && header_height >= 0.0 && gantt_header_height >= 0.0 &&
               footer_height >= 0.0 && title_size > 0.0 && subtitle_size >= 0.0 &&
               title_gap >= 0.0;
    }

    static PageSpec a4_landscape() noexcept { return PageSpec{}; }

    static PageSpec letter_landscape() noexcept {
        PageSpec spec;
        spec.width = 792.0;
        spec.height = 612.0;
        return spec;
    }
};

// One page's vertical bands, measured from the top of the paper.
struct PageLayout {
    PageSpec spec{};
    PrintKind kind = PrintKind::sheet;
    double title_baseline = 0.0;
    double subtitle_baseline = 0.0;
    double rule_y = 0.0;
    double body_top = 0.0;      // first pixel of the column/timescale header
    double body_bottom = 0.0;   // last pixel rows may use
    double footer_rule_y = 0.0;
    double footer_top = 0.0;
    double body_header_height = 0.0;  // header_height or gantt_header_height
    std::size_t rows_per_page = 1;    // always >= 1
};

// The page bands for `spec`.  Same formula as core/render/report.cpp plan_page().
PageLayout page_layout(const PageSpec& spec, PrintKind kind, bool has_subtitle) noexcept;

// Number of pages a report of `rows` rows occupies.  Always >= 1, even when
// there are no rows (core/render prints an empty page rather than nothing).
std::size_t page_count(std::size_t rows, std::size_t rows_per_page) noexcept;

// Header: "<project> - <report title>"; whichever part is empty is omitted.
std::string print_header_text(const std::string& project_name,
                              const std::string& report_title);

// Footer: "<date>  |  Page <page> of <total>"; the date is omitted when empty.
std::string print_footer_text(const std::string& date_text, std::size_t page,
                              std::size_t page_count);

// One print request: what to print and how the page looks.  `rows` are
// RecordSet row indexes in display order (the caller applies the filter the
// same way Export PDF does); an empty list still produces one page.
struct PrintRequest {
    PrintKind kind = PrintKind::sheet;
    std::string project_name;
    std::string report_title;
    std::string subtitle;
    std::string date_text;  // preformatted by the caller; never read from a clock here
    PageSpec spec{};
    std::vector<std::size_t> rows;
};

// One page of a built job: which slice of `PrintJob::rows` it holds, and the
// repeated header/footer text.  `first_row`/`row_count` index PrintJob::rows.
struct PrintPage {
    std::size_t index = 0;     // 0-based page number
    std::size_t first_row = 0;
    std::size_t row_count = 0;
    std::string header;
    std::string footer;

    bool operator==(const PrintPage& other) const = default;
};

// The paginated job.  Two calls to build_print_job() with the same request but
// different routes differ ONLY in `route`; page_layout_tests.cpp asserts that.
struct PrintJob {
    PrintKind kind = PrintKind::sheet;
    PrintRoute route = PrintRoute::printer;
    PageSpec spec{};
    PageLayout layout{};
    std::string subtitle;
    std::vector<std::size_t> rows;
    std::vector<PrintPage> pages;
};

// THE seam: the single function that decides pagination, for both the printer
// and the PDF-save route.  app/main.mm calls this and draws the pages; nothing
// equivalent exists anywhere else in the app.
PrintJob build_print_job(const PrintRequest& request, PrintRoute route);

}  // namespace app
