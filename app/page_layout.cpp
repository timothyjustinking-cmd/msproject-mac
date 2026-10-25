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

    // W29-3.1: the header switch removes the title/subtitle/rule band, so the
    // body starts at the top margin.  The subtitle lives in the header band.
    layout.title_baseline = spec.margin_top + spec.title_size;
    double below = layout.title_baseline + spec.title_gap;
    if (spec.show_header && has_subtitle) {
        layout.subtitle_baseline = below + spec.subtitle_size;
        below = layout.subtitle_baseline + kRuleGap;
    }
    if (spec.show_header) {
        layout.rule_y = below + kRuleGap;
        layout.body_top = layout.rule_y + kBandGap;
    } else {
        layout.title_baseline = 0.0;
        layout.subtitle_baseline = 0.0;
        layout.rule_y = 0.0;
        layout.body_top = spec.margin_top;
    }

    // The footer switch removes its whole 24 pt band; the bottom margin stays.
    layout.footer_rule_y = spec.height - spec.margin_bottom -
                           (spec.show_footer ? spec.footer_height : 0.0);
    layout.footer_top = spec.show_footer ? layout.footer_rule_y + kBandGap : 0.0;
    double bottom = layout.footer_rule_y;
    if (spec.show_footer) {
        bottom -= kBandGap;
    }
    // The legend is reserved above the footer band, with a gap before the body.
    if (spec.show_legend) {
        layout.legend_top = bottom - spec.legend_height;
        bottom = layout.legend_top - kBandGap;
    }
    layout.body_bottom = bottom;

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

std::string expand_print_text(const std::string& pattern, const std::string& date_text,
                              std::size_t page, std::size_t last_page) {
    std::string text;
    text.reserve(pattern.size() + date_text.size());
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        const char letter = pattern[index];
        if (letter != '&' || index + 1 == pattern.size()) {
            text += letter;
            continue;
        }
        const char code = pattern[++index];
        switch (code) {
            case 'p':
                text += std::to_string(page);
                break;
            case 'P':
                text += std::to_string(last_page);
                break;
            case 'd':
                text += date_text;
                break;
            case '&':
                text += '&';
                break;
            default:
                text += '&';
                text += code;
                break;
        }
    }
    return text;
}

std::vector<std::size_t> print_row_list(std::size_t record_row_count,
                                        const std::vector<std::size_t>& filter_rows,
                                        bool filter_active) {
    if (filter_active) {
        return filter_rows;  // an empty active list prints nothing, not everything
    }
    std::vector<std::size_t> rows;
    rows.reserve(record_row_count);
    for (std::size_t row = 0; row < record_row_count; ++row) {
        rows.push_back(row);
    }
    return rows;
}

PrintJob build_print_job(const PrintRequest& request, PrintRoute route) {
    PrintJob job;
    job.kind = request.kind;
    job.route = route;
    // W29-3.1: the job keeps the physical paper (the printer and the preview's
    // white rectangle) while the bands are measured on the scaled virtual page.
    job.spec = request.spec;
    job.subtitle = request.subtitle;
    job.rows = request.rows;
    job.layout = page_layout(request.spec.layout_spec(), request.kind,
                             request.spec.show_header && !request.subtitle.empty());

    const std::size_t per_page = std::max<std::size_t>(1, job.layout.rows_per_page);
    const std::size_t pages = page_count(job.rows.size(), per_page);
    const std::size_t first_page =
        static_cast<std::size_t>(std::max(1, request.spec.first_page_number));
    const std::size_t last_page = first_page + pages - 1;
    const std::string automatic_header =
        print_header_text(request.project_name, request.report_title);

    job.pages.reserve(pages);
    for (std::size_t page = 0; page < pages; ++page) {
        PrintPage entry;
        entry.index = page;
        entry.first_row = page * per_page;
        entry.row_count = std::min(per_page, job.rows.size() -
                                                 std::min(entry.first_row, job.rows.size()));
        const std::size_t number = first_page + page;
        entry.header = request.spec.header_text.empty()
                           ? automatic_header
                           : expand_print_text(request.spec.header_text,
                                               request.date_text, number, last_page);
        entry.footer = request.spec.footer_text.empty()
                           ? print_footer_text(request.date_text, number, last_page)
                           : expand_print_text(request.spec.footer_text,
                                               request.date_text, number, last_page);
        job.pages.push_back(std::move(entry));
    }
    return job;
}

}  // namespace app
