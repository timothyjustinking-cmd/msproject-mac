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
#include <string>

#include "cell_format.h"
#include "fixture_guard.h"
#include "gantt_style.h"
#include "view_style.h"

namespace {

using msproject::viewstate::BarStyle;
using msproject::viewstate::CellFormat;
using msproject::viewstate::CellFormatTable;
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

// Wave 31 (W31-3.6): the Resource Sheet's slot choice.  A resource row is
// either overallocated (slot 3) or not (slot 2); Project stores those slots in
// its own view record and the Qt row painter maps them without inventing a red.
void test_resource_style_lookup() {
    using msproject::viewstate::ViewTextStyles;
    ViewTextStyles style;
    check(app::text_style_for_resource_row(style, true) == nullptr,
          "view style: an undecoded resource record yields no style");
    style.decoded = true;
    style.text[static_cast<std::size_t>(TextStyleItem::non_critical)] =
        make_style("Calibri", 11, false, false, Rgb{});
    TextStyle overallocated = make_style("Calibri", 11, false, true, Rgb{165, 0, 0});
    overallocated.bold = true;
    style.text[static_cast<std::size_t>(TextStyleItem::critical)] = overallocated;

    check(app::text_style_item_for_resource_row(false) == TextStyleItem::non_critical,
          "view style: an ordinary resource takes the non-critical slot");
    check(app::text_style_item_for_resource_row(true) == TextStyleItem::critical,
          "view style: an overallocated resource takes the critical slot");
    const TextStyle* plain = app::text_style_for_resource_row(style, false);
    const TextStyle* over = app::text_style_for_resource_row(style, true);
    check(plain != nullptr && !plain->bold && !plain->has_colour,
          "view style: the ordinary resource is upright with automatic colour");
    check(over != nullptr && over->bold && over->has_colour &&
              over->colour == (Rgb{165, 0, 0}),
          "view style: the overallocated resource is the stored bold red");
    style.text[static_cast<std::size_t>(TextStyleItem::critical)].valid = false;
    check(app::text_style_for_resource_row(style, true) == nullptr,
          "view style: an unresolved font base yields no resource style");
}

// Wave 29 (W29-1.2): the fill rule the sheet model applies to one cell.
void test_cell_fill_rule() {
    CellFormat specific;
    specific.row_uid = 7;
    specific.field_id = 0x0b40000e;
    specific.has_background = true;
    specific.background = Rgb{255, 242, 0};
    CellFormat row_default;
    row_default.row_uid = 7;
    row_default.field_id = 0xffffffffu;
    row_default.has_background = true;
    row_default.background = Rgb{82, 87, 91};
    CellFormat automatic;
    automatic.row_uid = 7;
    automatic.field_id = 0x0b4000cd;
    automatic.has_background = false;
    CellFormatTable formats;
    formats.decoded = true;
    formats.entries = {specific, row_default, automatic};

    const app::CellFill stored = app::cell_fill_for(formats, 7, 0x0b40000e);
    check(stored.set && stored.colour == (Rgb{255, 242, 0}),
          "view style: a field-specific fill wins and is the stored colour");
    const app::CellFill fallback = app::cell_fill_for(formats, 7, 0x0b400020);
    check(fallback.set && fallback.colour == (Rgb{82, 87, 91}),
          "view style: an unformatted field takes the row default");
    check(!app::cell_fill_for(formats, 7, 0x0b4000cd).set,
          "view style: an automatic entry leaves the Text Styles background");
    check(!app::cell_fill_for(formats, 8, 0x0b40000e).set,
          "view style: formatting never leaks to another row");
}

// Wave 29 (W29-1.3): the stored bar styles' Show For masks decide which label
// each side shows.  Synthetic style tables, so every rule is isolated.
void test_bar_label_matching() {
    GanttStyle style;
    style.decoded = true;
    BarStyle critical;
    critical.show_for = 0x9;                 // Normal + Critical
    critical.right_text = 0x0b40000e;        // Name
    BarStyle summary;
    summary.show_for = 0x4;                  // Summary
    summary.left_text = 0x0b40000e;          // Name
    BarStyle milestone;
    milestone.show_for = 0x2;                // Milestone
    milestone.left_text = 0x0b400505;        // StartText
    milestone.right_text = 0x0b40000e;       // Name
    BarStyle auto_task;
    auto_task.show_for = 0x1;                // Normal
    auto_task.show_for_not = std::uint64_t{1} << 44;  // not Manually Scheduled
    auto_task.right_text = 0x0b400031;       // ResourceNames
    style.bars = {critical, summary, milestone, auto_task};

    app::BarLabelValues values;
    values.name = "NAME";
    values.start = "START";
    values.resource = "RES";

    app::BarLabelTask normal;
    normal.critical = true;
    app::BarLabelTexts texts = app::bar_label_texts_for(style, normal, values);
    check(texts.right == "NAME" && texts.left.empty(),
          "bar label: a critical normal task shows the Name on the right");

    app::BarLabelTask noncritical;
    texts = app::bar_label_texts_for(style, noncritical, values);
    check(texts.right == "RES" && texts.left.empty(),
          "bar label: a noncritical task matches no Normal+Critical style, so no Name");

    app::BarLabelTask summary_task;
    summary_task.summary = true;
    texts = app::bar_label_texts_for(style, summary_task, values);
    check(texts.left == "NAME" && texts.right.empty(),
          "bar label: a summary shows the left Name, not a normal task's label");

    app::BarLabelTask milestone_task;
    milestone_task.milestone = true;
    texts = app::bar_label_texts_for(style, milestone_task, values);
    check(texts.left == "START" && texts.right == "NAME",
          "bar label: a milestone shows StartText on the left and Name on the right");

    // The project row is also a summary task in the plan (project_summary_row
    // sets both flags); it must still match neither Summary nor Normal.
    app::BarLabelTask project;
    project.summary = true;
    project.project_summary = true;
    texts = app::bar_label_texts_for(style, project, values);
    check(texts.empty(),
          "bar label: the project row matches neither Summary nor Normal");

    // Active (43) is answered true; Manually Scheduled (44) from the model bit.
    app::BarLabelTask automatic;  // Normal + Active + not manual
    texts = app::bar_label_texts_for(style, automatic, values);
    check(texts.right == "RES",
          "bar label: a stock Active-tagged Normal style matches an auto task");
    app::BarLabelTask manual_task;
    manual_task.manually_scheduled = true;
    texts = app::bar_label_texts_for(style, manual_task, values);
    check(texts.right.empty(),
          "bar label: a Not Manually Scheduled style excludes a manual task");

    // An unanswerable category (Flag1 = bit 16) never matches.
    GanttStyle flagged_style;
    flagged_style.decoded = true;
    BarStyle flagged;
    flagged.show_for = std::uint64_t{1} << 16;  // Flag1
    flagged.right_text = 0x0b40000e;
    flagged_style.bars = {flagged};
    texts = app::bar_label_texts_for(flagged_style, automatic, values);
    check(texts.empty(),
          "bar label: a style testing a category the port cannot answer never matches");

    // First matching style claims the side; a later style fills only a side the
    // first left empty.  This is what keeps a Summary label under a
    // Summary+Critical style that stores no text.
    GanttStyle layered;
    layered.decoded = true;
    BarStyle specific;
    specific.show_for = 0x4 | 0x8;  // Summary + Critical
    specific.right_text = 0x0b40000e;
    BarStyle general;
    general.show_for = 0x4;         // Summary
    general.left_text = 0x0b40000e;
    layered.bars = {specific, general};
    app::BarLabelTask critical_summary;
    critical_summary.summary = true;
    critical_summary.critical = true;
    texts = app::bar_label_texts_for(layered, critical_summary, values);
    check(texts.right == "NAME" && texts.left == "NAME",
          "bar label: each side takes the first matching style that stores a field");

    // A field id the port cannot map claims the side with no text rather than
    // borrowing the next style's field.
    GanttStyle unknown;
    unknown.decoded = true;
    BarStyle unknown_first;
    unknown_first.show_for = 0x1;
    unknown_first.right_text = 0x0b499999;  // not a Task field the port knows
    BarStyle known_second;
    known_second.show_for = 0x1;
    known_second.right_text = 0x0b40000e;
    unknown.bars = {unknown_first, known_second};
    texts = app::bar_label_texts_for(unknown, automatic, values);
    check(texts.right.empty(),
          "bar label: an unmapped field id does not borrow a later style's field");
}

void check_stock_fixture() {
    GanttStyle style;
    CellFormatTable formats;
    std::string reason;
    const std::string path = std::string(APP_TEST_DATA_DIR) + "/synthetic-base.mpp";
    check(app::read_gantt_style(path, style, &reason, &formats),
          "view style: synthetic-base's Gantt style decodes (" + reason + ")");
    check(!formats.decoded && formats.entries.empty(),
          "view style: synthetic-base stores no per-cell formatting record");
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

    // Wave 29 (W29-1.3): a stock project's bar styles.  "Task" shows
    // ResourceNames for a Normal+Active task that is NOT Manually Scheduled
    // (bit 44) or Group By Summary (bit 40); "Manual Task" is the same for a
    // manual task; "Milestone" shows the finish date (0x0b400504, which wave
    // 29's Bar Styles capture proves is Task Finish) on the right unless the
    // task is manual or a placeholder; "Summary" stores no label.
    app::BarLabelValues values;
    values.name = "NAME";
    values.start = "START";
    values.finish = "FINISH";
    values.resource = "RES";
    const app::BarLabelTask auto_task;
    app::BarLabelTexts texts = app::bar_label_texts_for(style, auto_task, values);
    check(texts.right == "RES" && texts.left.empty(),
          "view style: a stock auto task shows ResourceNames on the right");
    app::BarLabelTask manual_task;
    manual_task.manually_scheduled = true;
    texts = app::bar_label_texts_for(style, manual_task, values);
    check(texts.right == "RES",
          "view style: a stock manual task shows ResourceNames via Manual Task");
    app::BarLabelTask milestone_task;
    milestone_task.milestone = true;
    texts = app::bar_label_texts_for(style, milestone_task, values);
    check(texts.right == "FINISH" && texts.left.empty(),
          "view style: a stock milestone shows the finish date on the right");
    app::BarLabelTask summary_task;
    summary_task.summary = true;
    texts = app::bar_label_texts_for(style, summary_task, values);
    check(texts.empty(), "view style: a stock summary row shows no bar label");
}

// Wave 31 (W31-3.4): the Resource Sheet view's own record on the committed
// fixture (tests/data/wave31-resource-sheet.mpp).  Project 2021 stored the view
// "Resource &Sheet"/"Resource Sheet", table "&Entry", filter "&All Resources";
// its 1,530-byte style record holds the 19 Text Styles slots read in
// build/w31/W31-G3-fonts.log.  Slot 2 is the ordinary-resource text (Calibri
// 11, automatic colours) and slot 3 the overallocated one (Calibri 11 bold,
// RGB 165,0,0) -- the red the Project capture shows on the fixture's Crew A
// row.  Those two slots are what W31-3.6 paints; this test pins that the file
// really stores them.
void check_resource_sheet_fixture() {
    app::ResourceSheetStyle style;
    std::string reason;
    const std::string path =
        std::string(APP_TEST_DATA_DIR) + "/wave31-resource-sheet.mpp";
    check(app::read_resource_sheet_style(path, style, &reason),
          "resource style: the committed fixture's Resource Sheet view decodes (" +
              reason + ")");
    check(style.decoded, "resource style: the view is marked decoded");
    check(style.view_name == "Resource &Sheet",
          "resource style: the stored view name");
    check(style.display_name == "Resource Sheet",
          "resource style: the stored display name");
    check(style.table_name == "&Entry", "resource style: the view names '&Entry'");
    check(style.filter_name == "&All Resources",
          "resource style: the view names the '&All Resources' filter");
    check(style.text.fonts.size() == 64,
          "resource style: the shared FONT_BASES table has 64 slots");
    check(style.text.text.size() == msproject::viewstate::kTextStyleItemCount,
          "resource style: all 19 text-style slots decode");

    const auto& normal = style.text.text[static_cast<std::size_t>(
        msproject::viewstate::TextStyleItem::non_critical)];
    check(normal.valid && normal.font_name == "Calibri" && normal.point_size == 11 &&
              !normal.bold,
          "resource style: slot 2 is the upright Calibri 11 row style");
    check(!normal.has_colour && !normal.has_background,
          "resource style: slot 2's colours are automatic");

    const auto& overallocated = style.text.text[static_cast<std::size_t>(
        msproject::viewstate::TextStyleItem::critical)];
    check(overallocated.valid && overallocated.font_name == "Calibri" &&
              overallocated.point_size == 11 && overallocated.bold,
          "resource style: slot 3 is bold Calibri 11");
    check(overallocated.has_colour &&
              overallocated.colour == (Rgb{165, 0, 0}),
          "resource style: slot 3 is the stored RGB 165,0,0");
    check(!overallocated.has_background,
          "resource style: slot 3's background is automatic");

    const auto& titles = style.text.text[static_cast<std::size_t>(
        msproject::viewstate::TextStyleItem::row_column_titles)];
    check(titles.valid && titles.font_name == "Segoe UI" && titles.point_size == 9,
          "resource style: slot 1 is the Segoe UI 9 title style");
    const auto& summary = style.text.text[static_cast<std::size_t>(
        msproject::viewstate::TextStyleItem::summary)];
    check(summary.has_background && summary.background == (Rgb{0xef, 0xf3, 0xff}),
          "resource style: slot 4 keeps the stored pale-blue fill");

    // W31-3.6: the slot choice the Qt row painter makes on this record.
    const msproject::viewstate::TextStyle* normal_row =
        app::text_style_for_resource_row(style.text, false);
    const msproject::viewstate::TextStyle* overallocated_row =
        app::text_style_for_resource_row(style.text, true);
    check(normal_row == &normal && !normal_row->has_colour,
          "resource style: an ordinary row resolves to the fixture's slot 2");
    check(overallocated_row == &overallocated && overallocated_row->bold &&
              overallocated_row->colour == (Rgb{165, 0, 0}),
          "resource style: an overallocated row resolves to slot 3's bold red");
    msproject::viewstate::ViewTextStyles undecoded;
    check(app::text_style_for_resource_row(undecoded, true) == nullptr,
          "resource style: a file without the view keeps the stock presentation");
}

void check_corpus_fixture() {
    const std::string path = std::string(APP_CORPUS_DIR) + "/project_a.mpp";
    if (!fixture::corpus_present(path, "view style corpus checks")) {
        return;
    }
    GanttStyle style;
    CellFormatTable formats;
    std::string reason;
    check(app::read_gantt_style(path, style, &reason, &formats),
          "view style: PROJECT_A's Gantt style decodes (" + reason + ")");

    // Wave 29 (W29-1.2): PROJECT_A's per-cell formatting.  213 entries are
    // 13 project-row entries (field overrides plus the row default) and 10 for
    // each of the 20 stored task rows; the capture's grey project/summary rows
    // and yellow Highlighted row are these stored fills.
    check(formats.decoded, "view style: PROJECT_A's cell-format record decodes");
    check(formats.entries.size() == 213,
          "view style: PROJECT_A stores 213 cell-format entries");
    const CellFormat* highlighted = formats.find(10742, 0x0b40000e);
    check(highlighted != nullptr && highlighted->has_background &&
              highlighted->background == (Rgb{255, 242, 0}),
          "view style: uid 10742 (ID 10) stores the yellow Highlighted fill");
    check(highlighted != nullptr && highlighted->background_pattern == 1,
          "view style: the Highlighted fill is solid");
    const CellFormat* indicators = formats.find(10742, 0x0b4000cd);
    check(indicators != nullptr && !indicators->has_background,
          "view style: the Highlighted row's hidden Indicators entry is automatic");
    const CellFormat* summary_cell = formats.find(10720, 0x0b40000e);
    check(summary_cell != nullptr &&
              summary_cell->background == (Rgb{82, 87, 91}),
          "view style: uid 10720 (ID 1) stores the summary row's grey");
    const CellFormat* project = formats.find(0, 0x0b40000e);
    check(project != nullptr && project->background == (Rgb{82, 87, 91}),
          "view style: the project row's Task Name entry is grey");
    const CellFormat* project_default = formats.find(0, 0x0b400001);
    check(project_default != nullptr &&
              project_default->field_id == 0xffffffffu &&
              project_default->background == (Rgb{0, 55, 103}),
          "view style: the project row's 0xffffffff default is the dark blue");
    const CellFormat* plain = formats.find(10733, 0x0b40000e);
    check(plain != nullptr && !plain->has_background,
          "view style: an automatic task row stores no fill");
    check(app::cell_fill_for(formats, 10742, 0x0b40000e).set,
          "view style: the fill rule returns the Highlighted row's colour");
    check(!app::cell_fill_for(formats, 10742, 0x0b4000cd).set,
          "view style: the fill rule treats the automatic entry as no fill");

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

    // Wave 29 (W29-1.3): four of the ten stored bar styles carry labels, and
    // these are their Show For masks and field ids as the record stores them.
    // Critical Task/Non-Critical Task show the Name on the right for
    // Normal+Critical / Normal+Noncritical; Milestone shows StartText
    // (0x0b400505) on the left and the Name on the right; Summary shows the
    // Name on the left.  Project Summary stores no label -- and the capture's
    // project row shows none.
    const auto bar_named = [&style](const char* name) -> const BarStyle* {
        for (const BarStyle& bar : style.bars) {
            if (bar.name == name) {
                return &bar;
            }
        }
        return nullptr;
    };
    // A side the style does not label stores the record's 0xffffffff sentinel
    // (the probe's "none"), not 0.
    constexpr std::uint32_t kNoLabelField = 0xffffffffu;
    const BarStyle* critical_bar = bar_named("Critical Task");
    check(critical_bar != nullptr && critical_bar->show_for == 0x9 &&
              critical_bar->right_text == 0x0b40000e &&
              critical_bar->left_text == kNoLabelField &&
              critical_bar->top_text == kNoLabelField &&
              critical_bar->bottom_text == kNoLabelField &&
              critical_bar->inside_text == kNoLabelField,
          "view style: PROJECT_A's Critical Task shows Name on the right");
    const BarStyle* noncritical_bar = bar_named("Non-Critical Task");
    check(noncritical_bar != nullptr && noncritical_bar->show_for == 0x11 &&
              noncritical_bar->right_text == 0x0b40000e,
          "view style: PROJECT_A's Non-Critical Task shows Name on the right");
    const BarStyle* milestone_bar = bar_named("Milestone");
    check(milestone_bar != nullptr && milestone_bar->show_for == 0x2 &&
              milestone_bar->left_text == 0x0b400505 &&
              milestone_bar->right_text == 0x0b40000e,
          "view style: PROJECT_A's Milestone shows StartText left, Name right");
    const BarStyle* summary_bar = bar_named("Summary");
    check(summary_bar != nullptr && summary_bar->show_for == 0x4 &&
              summary_bar->left_text == 0x0b40000e &&
              summary_bar->right_text == kNoLabelField,
          "view style: PROJECT_A's Summary shows Name on the left");
    const BarStyle* project_bar = bar_named("Project Summary");
    check(project_bar != nullptr && project_bar->show_for == 0x8000000 &&
              project_bar->left_text == kNoLabelField &&
              project_bar->right_text == kNoLabelField,
          "view style: PROJECT_A's Project Summary stores no label");

    app::BarLabelValues labels;
    labels.name = "NAME";
    labels.start = "START";
    labels.finish = "FINISH";
    labels.duration = "DUR";
    labels.resource = "RES";
    labels.percent = "PCT";
    app::BarLabelTask normal_critical;
    normal_critical.critical = true;
    const app::BarLabelTask normal_plain;
    app::BarLabelTask summary_task;
    summary_task.summary = true;
    app::BarLabelTask milestone_task;
    milestone_task.milestone = true;
    app::BarLabelTask project_task;
    project_task.summary = true;
    project_task.project_summary = true;
    app::BarLabelTexts texts = app::bar_label_texts_for(style, normal_critical, labels);
    check(texts.right == "NAME" && texts.left.empty(),
          "view style: PROJECT_A's critical task label is the Name on the right");
    texts = app::bar_label_texts_for(style, normal_plain, labels);
    check(texts.right == "NAME" && texts.left.empty(),
          "view style: PROJECT_A's noncritical task label is the Name on the right");
    texts = app::bar_label_texts_for(style, summary_task, labels);
    check(texts.left == "NAME" && texts.right.empty(),
          "view style: PROJECT_A's summary label is the Name on the left");
    texts = app::bar_label_texts_for(style, milestone_task, labels);
    check(texts.left == "START" && texts.right == "NAME",
          "view style: PROJECT_A's milestone label is the start date and the Name");
    texts = app::bar_label_texts_for(style, project_task, labels);
    check(texts.empty(), "view style: PROJECT_A's project row shows no bar label");

    // Wave 26 (W26-3.3): the file's three timescale tiers -- year, quarter
    // and month, exactly the capture's rows ("2026" / "Qtr 2, 2026" / "Apr").
    using msproject::viewstate::TimescaleUnit;
    check(style.top.unit == TimescaleUnit::years,
          "view style: PROJECT_A's top tier is years");
    check(style.top.format == 0, "view style: PROJECT_A's year format is YYYY");
    check(style.middle.unit == TimescaleUnit::quarters,
          "view style: PROJECT_A's middle tier is quarters");
    check(style.middle.format == 3,
          "view style: PROJECT_A's quarter format is 'Qtr 1, 2002'");
    check(style.bottom.unit == TimescaleUnit::months,
          "view style: PROJECT_A's bottom tier is months");
    check(style.bottom.format == 10,
          "view style: PROJECT_A's month format is 'Jan, Feb, ...'");
    check(style.top.count == 1 && style.middle.count == 1 && style.bottom.count == 1,
          "view style: every PROJECT_A tier stores count 1");
}

void test_refusals() {
    GanttStyle style;
    std::string reason;
    check(!app::read_gantt_style(std::string(APP_TEST_DATA_DIR) +
                                    "/does-not-exist.mpp",
                                style, &reason),
          "view style: a missing file is refused");
    check(!reason.empty(), "view style: the refusal carries a reason");

    app::ResourceSheetStyle resource;
    check(!app::read_resource_sheet_style(std::string(APP_TEST_DATA_DIR) +
                                              "/does-not-exist.mpp",
                                          resource, &reason),
          "resource style: a missing file is refused");
    check(!reason.empty(), "resource style: the refusal carries a reason");
}

}  // namespace

int main() {
    test_category_precedence();
    test_style_lookup();
    test_resource_style_lookup();
    test_cell_fill_rule();
    test_bar_label_matching();
    check_stock_fixture();
    check_resource_sheet_fixture();
    check_corpus_fixture();
    test_refusals();
    if (g_failures == 0) {
        std::printf("PASS: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    std::fprintf(stderr, "FAIL: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
}
