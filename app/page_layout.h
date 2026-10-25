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
//
// WAVE 29 (W29-3.1): PAGE SETUP
//   File > Print > Page Setup edits this record: four margins, orientation and
//   paper (width/height), scaling, header/footer switches, custom text and
//   alignment, the legend band and the first page number.  The dialog and the
//   print preview are both views of PageSpec, so what the preview paints is what
//   the printer draws.  Scaling is expressed as a virtual page: layout_spec()
//   divides the content box by the scale and the painters apply the matching
//   transform, so text and rows shrink while the paper margins stay put.
//   Export PDF is a separate route with core/render's single uniform margin and
//   no scaling control; it keeps using the A4/Letter defaults it always used.
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
//
// W29-3.1: the four margins replace the single `margin`; every other default is
// unchanged, so a default PageSpec still lays out exactly as it did.  The
// header/footer/legend switches and the text fields are the Page Setup dialog's
// state, and `first_page_number` is the number printed on page 0.
struct PageSpec {
    double width = 842.0;   // A4 landscape, points
    double height = 595.0;
    double margin_top = 24.0;
    double margin_bottom = 24.0;
    double margin_left = 24.0;
    double margin_right = 24.0;
    double scale_percent = 100.0;  // "Adjust to" in Page Setup, 10..400
    double row_height = 13.0;
    double header_height = 16.0;        // sheet column header
    double gantt_header_height = 26.0;  // core/render kGanttHeaderHeight
    double footer_height = 24.0;
    double title_size = 14.0;
    double subtitle_size = 8.0;
    double title_gap = 5.0;
    double legend_height = 26.0;

    bool show_header = true;
    bool show_footer = true;
    bool show_legend = false;
    // Custom text; empty means the automatic text print_header_text()/
    // print_footer_text() builds.  Codes: &p printed page number, &P last
    // printed page number, &d date text, && a literal ampersand.
    std::string header_text;
    std::string footer_text;
    std::string legend_text;
    int header_align = 0;  // 0 left, 1 centre, 2 right
    int footer_align = 2;
    int first_page_number = 1;

    // The width actually available to the table/chart.
    double printable_width() const noexcept { return width - margin_left - margin_right; }

    bool valid() const noexcept {
        return width > 0.0 && height > 0.0 && margin_top >= 0.0 && margin_bottom >= 0.0 &&
               margin_left >= 0.0 && margin_right >= 0.0 &&
               width > margin_left + margin_right && height > margin_top + margin_bottom &&
               scale_percent >= 10.0 && scale_percent <= 1000.0 && row_height > 0.0 &&
               header_height >= 0.0 && gantt_header_height >= 0.0 && footer_height >= 0.0 &&
               title_size > 0.0 && subtitle_size >= 0.0 && title_gap >= 0.0 &&
               legend_height >= 0.0 && header_align >= 0 && header_align <= 2 &&
               footer_align >= 0 && footer_align <= 2 && first_page_number >= 1;
    }

    // The scaling factor, always > 0.
    double scale() const noexcept {
        return scale_percent > 0.0 ? scale_percent / 100.0 : 1.0;
    }

    // The coordinate space page_layout() works in: the content box is divided
    // by the scale while the margins stay physical, so a painter transform of
    // translate(left, top) * scale(s) * translate(-left, -top) maps this page
    // back onto the paper.
    PageSpec layout_spec() const noexcept {
        PageSpec spec = *this;
        if (scale() != 1.0) {
            const double factor = scale();
            spec.width =
                margin_left + (width - margin_left - margin_right) / factor + margin_right;
            spec.height =
                margin_top + (height - margin_top - margin_bottom) / factor + margin_bottom;
            spec.scale_percent = 100.0;
        }
        return spec;
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
    double legend_top = 0.0;    // first pixel of the legend band; 0 when off
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

// Footer: "<date>  |  Page <page> of <pages>"; the date is omitted when empty.
std::string print_footer_text(const std::string& date_text, std::size_t page,
                              std::size_t pages);

// W29-3.1: expands a Page Setup header/footer pattern.  &p is the printed page
// number, &P the last printed page number (both shifted by first_page_number),
// &d the caller's date text and && a literal ampersand.  Any other &x is kept
// verbatim.  Pure, like the two automatic text functions above.
std::string expand_print_text(const std::string& pattern, const std::string& date_text,
                              std::size_t page, std::size_t last_page);

// The rows a print request covers: the filter's rows verbatim when a filter is
// active (an empty active list is a legitimate "nothing matches"), else every
// RecordSet row in order.
//
// WAVE 26 (W26-0.1).  These are RecordSet row indexes, NOT task indexes: when
// the file shows the project row, RecordSet row 0 is uid 0 and task N is on row
// N+1.  Building the list as `0 .. tasks.size()-1` drops the last task from the
// printed table and chart -- the wave-25 review's High finding.  The function
// lives here, in the shared layer, so both shells call it and the headless
// app_layout test can fail the old arithmetic on tests/data/wave24-order.mpp
// (macOS has no toolchain on this machine).
std::vector<std::size_t> print_row_list(std::size_t record_row_count,
                                        const std::vector<std::size_t>& filter_rows,
                                        bool filter_active);

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
// `spec` is the physical paper (the printer's page and the preview's white
// rectangle); `layout.spec` is the virtual page the bands are measured in -- it
// equals `spec` at 100 % scaling and is larger when the printout is reduced.
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
