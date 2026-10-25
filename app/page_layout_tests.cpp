// app/page_layout_tests.cpp -- the printed page arithmetic, proved headlessly.
//
// Three things this test pins:
//   1. the page geometry and rows-per-page for a 3-row and an 83-row plan, for
//      both the sheet and the Gantt report (the Gantt's two-tier header is
//      taller, so its page break is different);
//   2. the exact header/footer strings (project, report title, date, page i/n);
//   3. the SEAM: build_print_job() with PrintRoute::printer and PrintRoute::pdf
//      produces the same pages for the same request, and core/render (which
//      writes File > Export PDF) reports the same page count for the same rows.
//      That is what stops printing and Export PDF from drifting -- not a UI test.
//
// Synthetic rows only: no corpus, no client data.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "date.h"
#include "export_pdf.h"
#include "gantt.h"
#include "layout_model.h"
#include "page_layout.h"
#include "prefs.h"
#include "sheet.h"

namespace {

using app::PageSpec;
using app::PrintKind;
using app::PrintRequest;
using app::PrintRoute;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

app::PlanInput make_plan(std::size_t count) {
    app::PlanInput plan;
    plan.title = "Synthetic plan";
    plan.project_start = core::sched::from_ymd(2026, 1, 5);
    for (std::size_t index = 0; index < count; ++index) {
        app::TaskRow row;
        row.uid = static_cast<std::uint32_t>(index + 1);
        row.id = static_cast<std::uint32_t>(index + 1);
        row.outline_number = std::to_string(index + 1);
        row.name = "Task " + std::to_string(index + 1);
        row.duration_ticks = core::view::kTicksPerDay;
        row.start = core::sched::add_days(plan.project_start, static_cast<std::int32_t>(index));
        row.finish = row.start;
        plan.tasks.push_back(std::move(row));
    }
    return plan;
}

std::vector<std::size_t> every_row(std::size_t count) {
    std::vector<std::size_t> rows;
    rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        rows.push_back(index);
    }
    return rows;
}

PrintRequest make_request(PrintKind kind, const std::vector<std::size_t>& rows) {
    PrintRequest request;
    request.kind = kind;
    request.project_name = "Synthetic plan";
    request.report_title = kind == PrintKind::sheet ? "Sheet report" : "Gantt report";
    request.subtitle = "3 of 3 tasks";
    request.date_text = "22/10/26";
    request.spec = PageSpec::a4_landscape();
    request.rows = rows;
    return request;
}

void test_geometry() {
    const PageSpec spec = PageSpec::a4_landscape();
    check(spec.valid(), "the A4 landscape page spec is valid");
    check_size(static_cast<std::size_t>(spec.width), 842, "A4 landscape width");
    check_size(static_cast<std::size_t>(spec.height), 595, "A4 landscape height");
    check_size(static_cast<std::size_t>(spec.printable_width()), 794,
               "the printable width subtracts both margins");

    const app::PageLayout sheet = app::page_layout(spec, PrintKind::sheet, true);
    check(sheet.body_top > spec.margin_top, "the body starts below the title block");
    check(sheet.body_bottom < spec.height - spec.margin_bottom,
          "the body ends above the footer");
    check(sheet.body_header_height == 16.0, "the sheet body header is one 16 pt tier");
    check_size(sheet.rows_per_page, 36, "A4 landscape holds 36 sheet rows");

    const app::PageLayout gantt = app::page_layout(spec, PrintKind::gantt, true);
    check(gantt.body_header_height == 26.0,
          "the Gantt body header is core/render's two-tier 26 pt band");
    check_size(gantt.rows_per_page, 35, "the Gantt holds 35 rows on the same page");

    // The subtitle block is why body_top is below the title baseline; dropping it
    // must not add rows at these defaults (36 is still the floor of 468/13).
    const app::PageLayout no_subtitle = app::page_layout(spec, PrintKind::sheet, false);
    check_size(no_subtitle.rows_per_page, 36, "no subtitle still holds 36 sheet rows");

    const app::PageLayout letter =
        app::page_layout(PageSpec::letter_landscape(), PrintKind::sheet, true);
    check_size(letter.rows_per_page, 37, "Letter landscape holds 37 sheet rows");

    // A degenerate page can never produce zero rows per page (the renderers and
    // the print view both divide by it).
    PageSpec broken;
    broken.width = 0.0;
    check(!broken.valid(), "a zero-width page is invalid");
    check(app::page_layout(broken, PrintKind::sheet, true).rows_per_page >= 1,
          "an invalid page still holds at least one row");
}

void test_page_counts() {
    check_size(app::page_count(0, 36), 1, "an empty report is still one page");
    check_size(app::page_count(3, 36), 1, "3 rows fit on one page");
    check_size(app::page_count(36, 36), 1, "36 rows exactly fill one page");
    check_size(app::page_count(37, 36), 2, "the 37th row starts page 2");
    check_size(app::page_count(83, 36), 3, "83 sheet rows are three pages");
    check_size(app::page_count(83, 35), 3, "83 Gantt rows are three pages");
    check_size(app::page_count(83, 1), 83, "one row per page is 83 pages");
}

void test_texts() {
    check_text(app::print_header_text("Synthetic plan", "Sheet report"),
               "Synthetic plan - Sheet report", "the header names project and report");
    check_text(app::print_header_text("", "Gantt report"), "Gantt report",
               "an empty project name is omitted from the header");
    check_text(app::print_header_text("Synthetic plan", ""), "Synthetic plan",
               "an empty report title is omitted from the header");
    check_text(app::print_header_text("", ""), "", "two empty parts give an empty header");

    check_text(app::print_footer_text("22/10/26", 1, 3), "22/10/26  |  Page 1 of 3",
               "the footer names the date and the page");
    check_text(app::print_footer_text("22/10/26", 3, 3), "22/10/26  |  Page 3 of 3",
               "the footer numbers the last page");
    check_text(app::print_footer_text("", 2, 3), "Page 2 of 3",
               "an empty date leaves only the page text");

    // W29-3.1: Page Setup's text codes.
    check_text(app::expand_print_text("&p/&P", "22/10/26", 2, 5), "2/5",
               "&p and &P expand to the printed page numbers");
    check_text(app::expand_print_text("&d", "22/10/26", 1, 1), "22/10/26",
               "&d expands to the caller's date text");
    check_text(app::expand_print_text("a && b &x", "", 1, 1), "a & b &x",
               "&& is a literal ampersand and an unknown code is kept");
    check_text(app::expand_print_text("", "22/10/26", 1, 1), "",
               "an empty pattern stays empty");
    check_text(app::expand_print_text("tail&", "", 1, 1), "tail&",
               "a trailing ampersand is kept");
}

// W29-3.1: the four margins and the header/footer switches move the bands and
// the row count without touching the paper.
void test_page_setup_bands() {
    PageSpec spec = PageSpec::a4_landscape();
    spec.margin_left = 40.0;
    spec.margin_right = 40.0;
    check_size(static_cast<std::size_t>(spec.printable_width()), 762,
               "the printable width subtracts the left and right margins");
    app::PageLayout layout = app::page_layout(spec, PrintKind::sheet, true);
    check(layout.title_baseline == 38.0, "the side margins do not move the title");
    check(layout.body_top == 59.0, "the side margins do not move the body top");

    spec.margin_top = 48.0;
    spec.margin_bottom = 36.0;
    layout = app::page_layout(spec, PrintKind::sheet, true);
    check(layout.title_baseline == 62.0, "a 48 pt top margin lowers the title");
    check(layout.body_top == 83.0, "the body follows the title block down");
    check(layout.footer_rule_y == 535.0, "a 36 pt bottom margin raises the footer rule");
    check(layout.body_bottom == 531.0, "the body ends 4 pt above the footer rule");
    check_size(layout.rows_per_page, 33,
               "a taller top and bottom margin fit 33 sheet rows");

    PageSpec bare = PageSpec::a4_landscape();
    bare.show_header = false;
    layout = app::page_layout(bare, PrintKind::sheet, true);
    check(layout.title_baseline == 0.0 && layout.rule_y == 0.0,
          "a hidden header draws no title or rule");
    check(layout.body_top == bare.margin_top,
          "a hidden header starts the body at the top margin");
    check_size(layout.rows_per_page, 38, "a hidden header fits 38 sheet rows");

    bare.show_header = true;
    bare.show_footer = false;
    layout = app::page_layout(bare, PrintKind::sheet, true);
    check(layout.footer_rule_y == bare.height - bare.margin_bottom,
          "a hidden footer leaves only the bottom margin");
    check(layout.body_bottom == bare.height - bare.margin_bottom,
          "a hidden footer lets the body use the footer band");
    check_size(layout.rows_per_page, 38, "a hidden footer fits 38 sheet rows");
}

// W29-3.1: scaling divides the content box on the virtual page, so more rows
// fit while the physical paper (job.spec) is untouched.
void test_scaling() {
    PageSpec spec = PageSpec::a4_landscape();
    spec.scale_percent = 50.0;
    check(spec.valid(), "a 50 % page is valid");
    const PageSpec virtual_page = spec.layout_spec();
    check(virtual_page.width == 1636.0, "50 % doubles the virtual content width");
    check(virtual_page.height == 1142.0, "50 % doubles the virtual content height");
    check(virtual_page.scale_percent == 100.0, "the virtual page is itself unscaled");
    check(spec.scale() == 0.5, "the physical page keeps the 0.5 scale");

    app::PageLayout layout = app::page_layout(virtual_page, PrintKind::sheet, true);
    check_size(layout.rows_per_page, 78, "a 50 % printout fits 78 sheet rows");

    PrintRequest request = make_request(PrintKind::sheet, every_row(83));
    request.spec = spec;
    const app::PrintJob job = app::build_print_job(request, PrintRoute::printer);
    check(job.spec.width == 842.0 && job.spec.height == 595.0,
          "the job keeps the physical A4 paper");
    check(job.layout.spec.width == 1636.0, "the job lays out on the virtual page");
    check_size(job.pages.size(), 2, "83 rows at 50 % are two pages");

    request.spec.scale_percent = 200.0;
    const app::PrintJob large = app::build_print_job(request, PrintRoute::printer);
    check_size(large.layout.rows_per_page, 14, "a 200 % printout fits 14 sheet rows");
    check_size(large.pages.size(), 6, "83 rows at 200 % are six pages");

    PageSpec tiny = PageSpec::a4_landscape();
    tiny.scale_percent = 5.0;
    check(!tiny.valid(), "a 5 % scale is outside Page Setup's range");
}

// W29-3.1: the legend band is reserved above the footer and shrinks the body.
void test_legend() {
    PageSpec spec = PageSpec::a4_landscape();
    check(!spec.show_legend, "the legend is off by default");
    spec.show_legend = true;
    const app::PageLayout layout = app::page_layout(spec, PrintKind::sheet, true);
    check(layout.legend_top == 517.0, "the legend band sits 26 pt above the body");
    check(layout.body_bottom == 513.0, "the body ends 4 pt above the legend");
    check_size(layout.rows_per_page, 33, "the legend band costs three sheet rows");

    PrintRequest request = make_request(PrintKind::sheet, every_row(83));
    request.spec = spec;
    const app::PrintJob job = app::build_print_job(request, PrintRoute::printer);
    check_size(job.pages.size(), 3, "83 rows with a legend are still three pages");
}

// W29-3.1: first page number and the custom header/footer text Page Setup can
// set.  The page numbers shift; the automatic footer's "of N" is the last
// printed number.
void test_page_setup_text() {
    PrintRequest request = make_request(PrintKind::sheet, every_row(83));
    request.spec.first_page_number = 5;
    const app::PrintJob job = app::build_print_job(request, PrintRoute::printer);
    check_size(job.pages.size(), 3, "the numbered job has three pages");
    if (job.pages.size() == 3) {
        check_text(job.pages[0].footer, "22/10/26  |  Page 5 of 7",
                   "the first page prints its Page Setup number");
        check_text(job.pages[1].footer, "22/10/26  |  Page 6 of 7",
                   "the next page increments the printed number");
        check_text(job.pages[2].footer, "22/10/26  |  Page 7 of 7",
                   "the last page ends at first + pages - 1");
    }

    PrintRequest custom = make_request(PrintKind::sheet, every_row(3));
    custom.spec.first_page_number = 3;
    custom.spec.header_text = "&d plan &p/&P &&";
    custom.spec.footer_text = "&d page &p";
    const app::PrintJob text = app::build_print_job(custom, PrintRoute::printer);
    check_size(text.pages.size(), 1, "the custom-text job has one page");
    if (!text.pages.empty()) {
        check_text(text.pages[0].header, "22/10/26 plan 3/3 &",
                   "a custom header expands &d, &p, &P and &&");
        check_text(text.pages[0].footer, "22/10/26 page 3",
                   "a custom footer expands the date and page number");
    }
}

void test_three_row_job() {
    const PrintRequest request = make_request(PrintKind::sheet, every_row(3));
    const app::PrintJob job = app::build_print_job(request, PrintRoute::printer);
    check_size(job.pages.size(), 1, "3 rows are one page");
    if (job.pages.size() != 1) {
        return;
    }
    check_text(job.pages[0].header, "Synthetic plan - Sheet report",
               "the page repeats the header");
    check_text(job.pages[0].footer, "22/10/26  |  Page 1 of 1",
               "the page footer says 1 of 1");
    check_size(job.pages[0].first_row, 0, "page 1 starts at row 0");
    check_size(job.pages[0].row_count, 3, "page 1 holds all 3 rows");
    check(job.pages[0].index == 0, "the page index is zero-based");
}

void test_eighty_three_row_job() {
    const std::vector<std::size_t> rows = every_row(83);

    const app::PrintJob sheet =
        app::build_print_job(make_request(PrintKind::sheet, rows), PrintRoute::printer);
    check_size(sheet.pages.size(), 3, "83 sheet rows are three pages");
    if (sheet.pages.size() == 3) {
        check_size(sheet.pages[0].first_row, 0, "sheet page 0 starts at row 0");
        check_size(sheet.pages[0].row_count, 36, "sheet page 0 holds 36 rows");
        check_size(sheet.pages[1].first_row, 36, "sheet page 1 starts at row 36");
        check_size(sheet.pages[1].row_count, 36, "sheet page 1 holds 36 rows");
        check_size(sheet.pages[2].first_row, 72, "sheet page 2 starts at row 72");
        check_size(sheet.pages[2].row_count, 11, "sheet page 2 holds the last 11 rows");
        check_text(sheet.pages[1].footer, "22/10/26  |  Page 2 of 3",
                   "sheet page 2 of 3 is numbered");
    }

    const app::PrintJob gantt =
        app::build_print_job(make_request(PrintKind::gantt, rows), PrintRoute::printer);
    check_size(gantt.pages.size(), 3, "83 Gantt rows are three pages");
    if (gantt.pages.size() == 3) {
        check_size(gantt.pages[0].row_count, 35, "Gantt page 0 holds 35 rows");
        check_size(gantt.pages[1].first_row, 35, "Gantt page 1 starts at row 35");
        check_size(gantt.pages[2].first_row, 70, "Gantt page 2 starts at row 70");
        check_size(gantt.pages[2].row_count, 13, "Gantt page 2 holds the last 13 rows");
    }

    const app::PrintJob empty =
        app::build_print_job(make_request(PrintKind::sheet, {}), PrintRoute::printer);
    check_size(empty.pages.size(), 1, "an empty row list still makes one page");
    check_size(empty.pages[0].row_count, 0, "the empty page holds no rows");
}

void test_seam() {
    const PrintRequest request = make_request(PrintKind::gantt, every_row(83));
    const app::PrintJob printer = app::build_print_job(request, PrintRoute::printer);
    const app::PrintJob pdf = app::build_print_job(request, PrintRoute::pdf);

    check(printer.route == PrintRoute::printer, "the printer job is tagged printer");
    check(pdf.route == PrintRoute::pdf, "the PDF job is tagged pdf");
    check_size(printer.layout.rows_per_page, pdf.layout.rows_per_page,
               "both routes fit the same number of rows per page");
    check_size(printer.pages.size(), pdf.pages.size(),
               "both routes produce the same page count");
    check(printer.rows == pdf.rows, "both routes print the same rows");
    check(printer.pages == pdf.pages,
          "both routes produce identical pages (the seam: one layout function)");
    check(printer.subtitle == pdf.subtitle, "both routes repeat the same subtitle");

    // The same request laid out through the sheet kind differs only in the body
    // header, not in the seam itself.
    const app::PrintJob sheet =
        app::build_print_job(make_request(PrintKind::sheet, every_row(83)), PrintRoute::pdf);
    check_size(sheet.layout.rows_per_page, 36, "the sheet route holds 36 rows");
    check_size(printer.layout.rows_per_page, 35, "the Gantt route holds 35 rows");
}

// File > Export PDF writes through core/render.  If page_layout.h and
// core/render ever disagree about the bands, this test fails: the printed page
// count and the exported page count must be the same for the same rows.
void test_export_agrees() {
    app::PlanInput plan = make_plan(83);
    core::view::RecordSet records = app::make_record_set(plan);
    core::sheet::SheetFormat format{};
    app::Preferences prefs{};
    app::apply_preferences(prefs, format);

    for (const app::ReportKind kind :
         {app::ReportKind::sheet, app::ReportKind::gantt}) {
        app::ExportRequest request;
        request.kind = kind;
        request.records = &records;
        request.columns = app::default_sheet_columns();
        request.format = format;
        request.metrics = app::default_sheet_metrics();
        request.preferences = prefs;
        request.title = plan.title;
        request.subtitle = app::export_subtitle(kind, "", 83, 83);
        request.footer = app::export_footer(prefs, "");
        request.timescale = app::timescale_for_zoom(
            plan, core::gantt::GanttZoom::week);
        request.auto_timescale = true;
        request.working_day_shading = true;

        const std::string path =
            (std::filesystem::path(APP_TEST_OUT_DIR) /
             (kind == app::ReportKind::sheet ? "app-print-sheet.pdf"
                                             : "app-print-gantt.pdf"))
                .string();
        const app::ExportOutcome outcome = app::export_report(request, path);
        check(outcome.ok, kind == app::ReportKind::sheet
                              ? "the 83-row sheet report exports"
                              : "the 83-row Gantt report exports");
        if (!outcome.ok) {
            std::printf("  error: %s\n", outcome.error.c_str());
            continue;
        }
        check_size(outcome.rows, 83, "the export prints all 83 rows");

        app::PrintRequest print;
        print.kind = kind == app::ReportKind::sheet ? PrintKind::sheet : PrintKind::gantt;
        print.project_name = plan.title;
        print.report_title = kind == app::ReportKind::sheet ? "Sheet report" : "Gantt report";
        print.subtitle = request.subtitle;
        print.date_text = "22/10/26";
        print.spec = request.page;
        print.rows = every_row(83);
        const app::PrintJob job = app::build_print_job(print, PrintRoute::pdf);
        check_size(outcome.pages, job.pages.size(),
                   kind == app::ReportKind::sheet
                       ? "Export PDF and Print agree on the sheet page count"
                       : "Export PDF and Print agree on the Gantt page count");
    }
}

void test_request_is_not_mutated() {
    PrintRequest request = make_request(PrintKind::sheet, every_row(3));
    const std::vector<std::size_t> before = request.rows;
    (void)app::build_print_job(request, PrintRoute::printer);
    check(request.rows == before, "build_print_job leaves the request untouched");
    check_size(request.rows.size(), 3, "the request still names its rows");
}

}  // namespace

int main() {
    test_geometry();
    test_page_counts();
    test_texts();
    test_page_setup_bands();
    test_scaling();
    test_legend();
    test_page_setup_text();
    test_three_row_job();
    test_eighty_three_row_job();
    test_seam();
    test_export_agrees();
    test_request_is_not_mutated();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("page_layout_tests: %d checks passed\n", g_checks);
    return 0;
}
