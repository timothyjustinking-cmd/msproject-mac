// app/view_style_tests.cpp -- the file-style seam, proved headlessly.
//
// Two halves:
//   1. the row -> Text Styles category rule (the precedence measured from
//      PROJECT_A's capture: milestone beats critical, summary beats critical),
//      on synthetic rows only;
//   2. app::read_gantt_style() on the committed neutral fixture
//      tests/data/synthetic-base.mpp (the stock Project 2021 Gantt style), and
//      -- when the local corpus is present -- on corpus/project_a.mpp, whose
//      milestone/ summary colours are the two expected failures W26-3.1 closes.
//
// The corpus half is skipped, not failed, on a runner without corpus/ (the
// same rule the other corpus-dependent tests follow).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "gantt_style.h"
#include "view_style.h"

namespace {

using msproject::viewstate::FontBase;
using msproject::viewstate::GanttStyle;
using msproject::viewstate::Rgb;
using msproject::viewstate::TextStyle;
using msproject::viewstate::TextStyleItem;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool file_exists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

TextStyle make_style(const char* family, int size, bool italic, bool has_colour,
                     Rgb colour) {
    TextStyle style;
    style.valid = true;
    style.font_name = family;
    style.point_size = size;
    style.italic = italic;
    style.has_colour = has_colour;
    style.colour = colour;
    return style;
}

void test_category_precedence() {
    using msproject::viewstate::TextStyleItem;
    check(app::text_style_item_for_row(true, false, false, false) ==
              TextStyleItem::project_summary,
          "view style: the project row uses the project summary style");
    check(app::text_style_item_for_row(false, true, false, true) == TextStyleItem::summary,
          "view style: a critical summary uses the summary style");
    check(app::text_style_item_for_row(false, false, true, true) == TextStyleItem::milestone,
          "view style: a critical milestone uses the milestone style");
    check(app::text_style_item_for_row(false, false, false, true) == TextStyleItem::critical,
          "view style: a critical task uses the critical style");
    check(app::text_style_item_for_row(false, false, false, false) ==
              TextStyleItem::non_critical,
          "view style: a normal task uses the noncritical style");
}

void test_style_lookup() {
    GanttStyle style;
    check(app::text_style_for_row(style, false, false, false, false) == nullptr,
          "view style: an undecoded record yields no style");
    style.decoded = true;
    style.text[static_cast<std::size_t>(TextStyleItem::milestone)] =
        make_style("Calibri", 8, true, true, Rgb{255, 0, 0});
    const TextStyle* milestone =
        app::text_style_for_row(style, false, false, true, true);
    check(milestone != nullptr, "view style: the milestone style resolves");
    check(milestone != nullptr && milestone->italic, "view style: the milestone is italic");
    check(milestone != nullptr && milestone->has_colour &&
              milestone->colour == (Rgb{255, 0, 0}),
          "view style: the milestone colour is the record's red");
    check(app::text_style_for_row(style, false, false, false, true) == nullptr,
          "view style: an unset style is not invented");
    style.text[static_cast<std::size_t>(TextStyleItem::critical)].valid = false;
    check(app::text_style_for_row(style, false, false, false, true) == nullptr,
          "view style: a record with no font base yields no style");
}

void check_stock_fixture() {
    GanttStyle style;
    std::string reason;
    const std::string path = std::string(APP_TEST_DATA_DIR) + "/synthetic-base.mpp";
    check(app::read_gantt_style(path, style, &reason),
          "view style: synthetic-base's Gantt style decodes (" + reason + ")");
    check(style.decoded, "view style: synthetic-base's record is marked decoded");
    check(style.fonts.size() == 64, "view style: synthetic-base has 64 font slots");
    if (style.fonts.size() > 3) {
        check(style.fonts[3].valid && style.fonts[3].name == "Calibri" &&
                  style.fonts[3].point_size == 11,
              "view style: synthetic-base's noncritical font is Calibri 11");
    }
    const TextStyle& highlighted =
        style.text[static_cast<std::size_t>(TextStyleItem::highlighted)];
    check(highlighted.valid && highlighted.has_background &&
              highlighted.background == (Rgb{255, 255, 102}),
          "view style: synthetic-base's highlighted fill is the stored pale yellow");
    const TextStyle& summary =
        style.text[static_cast<std::size_t>(TextStyleItem::summary)];
    check(summary.valid && summary.bold, "view style: synthetic-base's summary is bold");
    const TextStyle& milestone =
        style.text[static_cast<std::size_t>(TextStyleItem::milestone)];
    check(milestone.valid && !milestone.italic && !milestone.has_colour,
          "view style: synthetic-base's stock milestone is upright and automatic");
    const TextStyle& project_summary =
        style.text[static_cast<std::size_t>(TextStyleItem::project_summary)];
    check(project_summary.valid && project_summary.point_size == 12 &&
              project_summary.bold,
          "view style: synthetic-base's project summary is Calibri 12 bold");

    // Wave 26 (W26-3.2): the stock bar palette.  A stock project has no
    // critical-specific, summary-critical or finished bar style; the shell
    // keeps its stock fallbacks for those roles.
    const app::BarPalette palette = app::bar_palette_from(style);
    check(palette.decoded, "view style: the stock bar palette decodes");
    check(palette.has_normal && palette.normal == (Rgb{138, 187, 237}),
          "view style: stock auto Task bar is (138,187,237)");
    check(palette.has_manual_normal &&
              palette.manual_normal == (Rgb{116, 204, 212}),
          "view style: stock Manual Task bar is (116,204,212)");
    check(palette.has_summary && palette.summary == (Rgb{84, 84, 84}),
          "view style: stock Summary bar is (84,84,84)");
    check(palette.has_milestone && palette.milestone == (Rgb{99, 99, 99}),
          "view style: stock Milestone diamond is the start colour (99,99,99)");
    check(palette.has_progress && palette.progress == (Rgb{59, 135, 212}),
          "view style: stock Progress bar is (59,135,212)");
    check(!palette.has_critical,
          "view style: a stock project defines no critical-specific bar style");
    check(!palette.has_summary_critical,
          "view style: a stock project defines no summary-critical bar style");
    check(!palette.has_finished,
          "view style: a stock project defines no finished bar style");
}

void check_corpus_fixture() {
    const std::string path = std::string(APP_CORPUS_DIR) + "/project_a.mpp";
    if (!file_exists(path)) {
        std::printf("SKIP: view style corpus checks (no corpus/project_a.mpp)\n");
        return;
    }
    GanttStyle style;
    std::string reason;
    check(app::read_gantt_style(path, style, &reason),
          "view style: PROJECT_A's Gantt style decodes (" + reason + ")");

    // The file's own text styles: a font table of Arial faces, a red italic
    // milestone, a bold blue summary and a navy project summary.  These are the
    // values the Qt sheet gate asserts against Project's capture.
    const TextStyle& milestone =
        style.text[static_cast<std::size_t>(TextStyleItem::milestone)];
    check(milestone.valid, "view style: PROJECT_A's milestone style resolves");
    check(milestone.italic, "view style: PROJECT_A's milestone is italic");
    check(milestone.has_colour && milestone.colour == (Rgb{255, 0, 0}),
          "view style: PROJECT_A's milestone colour is recorded red");
    const TextStyle& summary =
        style.text[static_cast<std::size_t>(TextStyleItem::summary)];
    check(summary.bold, "view style: PROJECT_A's summary is bold");
    check(summary.has_colour && summary.colour == (Rgb{51, 102, 255}),
          "view style: PROJECT_A's summary colour is recorded blue");
    const TextStyle& critical =
        style.text[static_cast<std::size_t>(TextStyleItem::critical)];
    check(critical.has_colour && critical.colour == (Rgb{0, 0, 0}),
          "view style: PROJECT_A's critical style colour is the stored black");
    const TextStyle& project_summary =
        style.text[static_cast<std::size_t>(TextStyleItem::project_summary)];
    check(project_summary.has_colour && project_summary.colour == (Rgb{0, 0, 128}),
          "view style: PROJECT_A's project summary colour is recorded navy");

    // PROJECT_A's own bar palette: the 10 active styles it keeps, with the
    // colours the clean capture shows (navy summaries, red critical bars,
    // dark-blue noncritical bars, green progress).  The file has no light-red
    // "Critical" bar in use: the capture contains zero (247,138,136) pixels.
    const app::BarPalette palette = app::bar_palette_from(style);
    check(palette.decoded, "view style: PROJECT_A's bar palette decodes");
    check(palette.has_normal && palette.normal == (Rgb{0, 32, 96}),
          "view style: PROJECT_A's noncritical bar is (0,32,96)");
    check(palette.has_critical && palette.critical == (Rgb{255, 0, 0}),
          "view style: PROJECT_A's critical bar is (255,0,0)");
    check(palette.has_summary && palette.summary == (Rgb{0, 0, 128}),
          "view style: PROJECT_A's summary bar is (0,0,128)");
    check(palette.has_summary_critical &&
              palette.summary_critical == (Rgb{255, 0, 0}),
          "view style: PROJECT_A's critical summary bar is (255,0,0)");
    check(palette.has_milestone && palette.milestone == (Rgb{255, 0, 0}),
          "view style: PROJECT_A's milestone diamond is (255,0,0)");
    check(palette.has_project_summary &&
              palette.project_summary == (Rgb{0, 0, 128}),
          "view style: PROJECT_A's project summary bar is (0,0,128)");
    check(palette.has_progress && palette.progress == (Rgb{0, 176, 80}),
          "view style: PROJECT_A's progress fill is (0,176,80)");
    check(palette.has_finished && palette.finished == (Rgb{0, 176, 80}),
          "view style: PROJECT_A's finished fill is (0,176,80)");
}

void test_refusals() {
    GanttStyle style;
    std::string reason;
    check(!app::read_gantt_style(std::string(APP_TEST_DATA_DIR) +
                                    "/does-not-exist.mpp",
                                style, &reason),
          "view style: a missing file is refused");
    check(!reason.empty(), "view style: the refusal carries a reason");
}

}  // namespace

int main() {
    test_category_precedence();
    test_style_lookup();
    check_stock_fixture();
    check_corpus_fixture();
    test_refusals();
    if (g_failures == 0) {
        std::printf("PASS: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    std::fprintf(stderr, "FAIL: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
}
