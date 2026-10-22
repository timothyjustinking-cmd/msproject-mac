// app/page_layout.cpp -- see page_layout.h.  Mirrors core/render/report.cpp's
// plan_page() band arithmetic; the constants are documented there and here.

#include "page_layout.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace app {
namespace {

// core/render/report.cpp: two rules, each 2 pt above/below the text blocks, and
// a 4 pt gap before the body and after the footer rule.
constexpr double kRuleGap = 2.0;
constexpr double kBandGap = 4.0;

}  // namespace

PageLayout page_layout(const PageSpec& spec, PrintKind kind, bool has_subtitle) noexcept {
    PageLayout layout;
    layout.spec = spec;
    layout.kind = kind;
    layout.rows_per_page = 1;

    layout.title_baseline = spec.margin + spec.title_size;
    double below = layout.title_baseline + spec.title_gap;
    if (has_subtitle) {
        layout.subtitle_baseline = below + spec.subtitle_size;
        below = layout.subtitle_baseline + kRuleGap;
    }
    layout.rule_y = below + kRuleGap;
    layout.body_top = layout.rule_y + kBandGap;
    layout.footer_rule_y = spec.height - spec.margin - spec.footer_height;
    layout.footer_top = layout.footer_rule_y + kBandGap;
    layout.body_bottom = layout.footer_rule_y - kBandGap;
    layout.body_header_height =
        kind == PrintKind::gantt ? spec.gantt_header_height : spec.header_height;

    const double area = layout.body_bottom - layout.body_top - layout.body_header_height;
    if (area > 0.0 && spec.row_height > 0.0) {
        layout.rows_per_page =
            std::max<std::size_t>(1, static_cast<std::size_t>(area / spec.row_height));
    }
    return layout;
}

std::size_t page_count(std::size_t rows, std::size_t rows_per_page) noexcept {
    const std::size_t per_page = std::max<std::size_t>(1, rows_per_page);
    if (rows == 0) {
        return 1;  // core/render prints one empty page rather than a zero-page PDF
    }
    return (rows + per_page - 1) / per_page;
}

std::string print_header_text(const std::string& project_name,
                              const std::string& report_title) {
    if (project_name.empty()) {
        return report_title;
    }
    if (report_title.empty()) {
        return project_name;
    }
    std::string text = project_name;
    text += " - ";
    text += report_title;
    return text;
}

std::string print_footer_text(const std::string& date_text, std::size_t page,
                              std::size_t pages) {
    std::string text;
    if (!date_text.empty()) {
        text += date_text;
        text += "  |  ";
    }
    text += "Page ";
    text += std::to_string(page);
    text += " of ";
    text += std::to_string(pages);
    return text;
}

PrintJob build_print_job(const PrintRequest& request, PrintRoute route) {
    PrintJob job;
    job.kind = request.kind;
    job.route = route;
    job.spec = request.spec;
    job.subtitle = request.subtitle;
    job.rows = request.rows;
    job.layout = page_layout(request.spec, request.kind, !request.subtitle.empty());

    const std::size_t per_page = std::max<std::size_t>(1, job.layout.rows_per_page);
    const std::size_t pages = page_count(job.rows.size(), per_page);
    const std::string header =
        print_header_text(request.project_name, request.report_title);

    job.pages.reserve(pages);
    for (std::size_t page = 0; page < pages; ++page) {
        PrintPage entry;
        entry.index = page;
        entry.first_row = page * per_page;
        entry.row_count = std::min(per_page, job.rows.size() -
                                                 std::min(entry.first_row, job.rows.size()));
        entry.header = header;
        entry.footer = print_footer_text(request.date_text, page + 1, pages);
        job.pages.push_back(std::move(entry));
    }
    return job;
}

}  // namespace app
