// app/selection_model_tests.cpp -- the headless proof of the sheet <-> Gantt
// selection mapping, the collapse behaviour the Gantt follows, and the editing
// path the shell's keys drive (begin_edit -> commit_edit -> undo, plus the
// read-only column policy).
//
// Synthetic rows only: no corpus, no .mpp, nothing client-derived.  This is the
// part of app shell v2 that can be proved on Windows; the AppKit half
// (mouseDown:/keyDown:, NSOpenPanel, NSSavePanel) is proved by the macOS CI job
// compiling main.mm, and by nothing else.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w38 -G "Visual Studio 17 2022" -A x64
//   cmake --build build\vs-w38 --config Release
//   ctest --test-dir build\vs-w38 -C Release -R "^app_selection$" --output-on-failure

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "date.h"
#include "document.h"
#include "gantt.h"
#include "layout_model.h"
#include "selection_model.h"
#include "sheet.h"

namespace {

using app::date32;
using app::kDateNa;
using core::sched::from_ymd;

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

void check_int(std::int64_t got, std::int64_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %lld want %lld\n", what, static_cast<long long>(got),
                    static_cast<long long>(want));
    }
}

void check_near(double got, double want, double tolerance, const char* what) {
    ++g_checks;
    if (!std::isfinite(got) || std::abs(got - want) > tolerance) {
        ++g_failures;
        std::printf("FAIL: %s: got %.6f want %.6f\n", what, got, want);
    }
}

void check_date(date32 got, date32 want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %s want %s\n", what, core::sched::to_string(got).c_str(),
                    core::sched::to_string(want).c_str());
    }
}

app::TaskRow make_task(std::uint32_t uid, std::uint32_t id, const char* outline, const char* name,
                       std::int64_t ticks, date32 start, date32 finish, std::uint16_t percent,
                       int level, bool summary, bool milestone, bool critical) {
    app::TaskRow row;
    row.uid = uid;
    row.id = id;
    row.outline_number = outline;
    row.name = name;
    row.duration_ticks = ticks;
    row.percent_complete = percent;
    row.start = start;
    row.finish = finish;
    row.outline_level = level;
    row.summary = summary;
    row.milestone = milestone;
    row.critical = critical;
    return row;
}

app::LinkRow make_link(std::uint32_t predecessor, std::uint32_t successor,
                       core::gantt::LinkType type) {
    app::LinkRow row;
    row.predecessor_uid = predecessor;
    row.successor_uid = successor;
    row.type = type;
    row.critical = false;
    return row;
}

// One summary (19-23 Oct) with two children (19-21 Oct, 22-23 Oct) and a
// milestone (26 Oct): the same shape as layout_model_tests.cpp's fixture, so the
// two files describe the same rows.
constexpr std::uint32_t kSummaryUid = 1;
constexpr std::uint32_t kFirstChildUid = 2;
constexpr std::uint32_t kSecondChildUid = 3;
constexpr std::uint32_t kMilestoneUid = 4;

app::PlanInput synthetic_plan() {
    app::PlanInput input;
    input.title = "Synthetic plan";
    input.source_name = "synthetic";
    input.project_start = from_ymd(2026, 10, 19, 8, 0);
    input.project_finish = from_ymd(2026, 10, 26, 17, 0);
    input.tasks.push_back(make_task(kSummaryUid, 1, "1", "Summary", 5 * 4800,
                                    from_ymd(2026, 10, 19, 8, 0),
                                    from_ymd(2026, 10, 23, 17, 0), 0, 0, true, false, false));
    input.tasks.push_back(make_task(kFirstChildUid, 2, "1.1", "First child", 3 * 4800,
                                    from_ymd(2026, 10, 19, 8, 0),
                                    from_ymd(2026, 10, 21, 17, 0), 50, 1, false, false, true));
    input.tasks.push_back(make_task(kSecondChildUid, 3, "1.2", "Second child", 2 * 4800,
                                    from_ymd(2026, 10, 22, 8, 0),
                                    from_ymd(2026, 10, 23, 17, 0), 100, 1, false, false, false));
    input.tasks.push_back(make_task(kMilestoneUid, 4, "1.3", "Milestone", 0,
                                    from_ymd(2026, 10, 26, 8, 0),
                                    from_ymd(2026, 10, 26, 17, 0), 0, 1, false, true, false));
    input.links.push_back(make_link(kFirstChildUid, kSecondChildUid,
                                    core::gantt::LinkType::finish_to_start));
    input.links.push_back(make_link(kSecondChildUid, kMilestoneUid,
                                    core::gantt::LinkType::finish_to_start));
    return input;
}

constexpr std::size_t kUidColumn = 0;
constexpr std::size_t kIdColumn = 1;
constexpr std::size_t kOutlineColumn = 2;
constexpr std::size_t kNameColumn = 3;
constexpr std::size_t kStartColumn = 4;

// Everything a view needs, in one place, so a test can rebuild it the way
// main.mm does after a collapse.
struct Fixture {
    core::view::RecordSet records{core::model::RecordType::Task};
    std::unique_ptr<core::sheet::SheetModel> sheet;
    app::RowMap map;
    core::gantt::GanttLayout gantt;
    core::sheet::SheetMetrics metrics = app::default_sheet_metrics();

    explicit Fixture(const app::PlanInput& input) {
        records = app::make_record_set(input);
        sheet = std::make_unique<core::sheet::SheetModel>(records, app::default_sheet_columns());
        app::apply_edit_policy(*sheet);
        rebuild(input);
    }

    void rebuild(const app::PlanInput& input) {
        map = app::build_row_map(*sheet);
        const core::gantt::GanttSource source =
            app::make_gantt_source_for_rows(input, map.rows, {}, kDateNa);
        gantt = core::gantt::build_gantt(source, app::default_gantt_style(),
                                         app::timescale_for_plan(input, 12.0));
    }
};

// ---------------------------------------------------------------------------

void test_row_map() {
    const app::PlanInput input = synthetic_plan();
    Fixture fixture(input);
    const app::RowMap& map = fixture.map;

    check_size(map.size(), 4, "map rows");
    check_size(map.rows.size(), map.uids.size(), "map rows and uids agree");
    check_size(map.row_at(0), 0, "gantt row 0 is sheet row 0");
    check_size(map.row_at(3), 3, "gantt row 3 is sheet row 3");
    check_size(map.row_at(4), core::sheet::kNoIndex, "gantt row past the end");
    check_size(map.ordinal_of(2), 2, "sheet row 2 is gantt row 2");
    check_size(map.ordinal_of(9), core::sheet::kNoIndex, "hidden/absent row has no ordinal");
    check_size(map.ordinal_of_uid(kSecondChildUid), 2, "uid 3 is gantt row 2");
    check_size(map.ordinal_of_uid(99), core::sheet::kNoIndex, "unknown uid has no ordinal");
    check(map.uids[1] == kFirstChildUid, "map carries the task uid");
}

void test_selection_and_scroll() {
    const app::PlanInput input = synthetic_plan();
    Fixture fixture(input);
    core::sheet::SheetModel& sheet = *fixture.sheet;

    check(!app::selected_task(sheet, fixture.map).has_selection, "no selection initially");

    check(app::select_cell(sheet, 2, kNameColumn), "select a row through the model");
    const app::SelectedTask selected = app::selected_task(sheet, fixture.map);
    check(selected.has_selection, "the model has a selection");
    check_size(selected.row, 2, "selected sheet row");
    check_size(selected.ordinal, 2, "selected gantt row");
    check(selected.uid == kSecondChildUid, "selected uid");
    check(selected.visible, "the selected row is visible");
    check(selected.uid == fixture.map.uids[selected.ordinal], "selected uid is the map's");

    // Scrolling: y is from the top of the document, header included.
    const app::ScrollTarget target = app::scroll_target(sheet, fixture.metrics, 2);
    check(target.valid, "scroll target is valid");
    const double row_height = static_cast<double>(fixture.metrics.row_height);
    const double header = static_cast<double>(fixture.metrics.header_height);
    check_near(target.y, header + 2.0 * row_height, 1e-9, "scroll y is header + 2 rows");
    check_near(target.height, row_height, 1e-9, "scroll height is one row");
    check_near(app::scroll_target(sheet, fixture.metrics, 0).y, header, 1e-9,
               "first row sits below the header");
    check(!app::scroll_target(sheet, fixture.metrics, 99).valid, "no target for a missing row");

    // Arrow-key navigation over the visible list, clamped.
    check_size(app::row_after(fixture.map, 2, 1), 3, "down one row");
    check_size(app::row_after(fixture.map, 2, -1), 1, "up one row");
    check_size(app::row_after(fixture.map, 0, -1), 0, "top is clamped");
    check_size(app::row_after(fixture.map, 3, 10), 3, "bottom is clamped");
    check_size(app::row_after(fixture.map, 9, 1), core::sheet::kNoIndex, "no row, no move");
}

void test_gantt_click() {
    const app::PlanInput input = synthetic_plan();
    Fixture fixture(input);
    core::sheet::SheetModel& sheet = *fixture.sheet;
    const core::gantt::GanttLayout& layout = fixture.gantt;

    const core::gantt::Bar* bar = layout.bar(kSecondChildUid);
    check(bar != nullptr, "the second child has a bar");
    if (bar != nullptr) {
        const std::size_t row = app::select_from_gantt(sheet, fixture.map, layout,
                                                       bar->rect.center_x(), bar->rect.center_y(),
                                                       kNameColumn);
        check_size(row, 2, "clicking a bar selects its sheet row");
        check_size(sheet.anchor().row, 2, "the model's anchor follows the click");
        check_size(sheet.anchor().column, kNameColumn, "the clicked column is the name column");
        check(app::selected_task(sheet, fixture.map).uid == kSecondChildUid,
              "selection round trips through the Gantt");
    }

    // A click in empty space changes nothing and reports the miss.
    const std::size_t before = sheet.anchor().row;
    const double far_x = layout.content_width + 500.0;
    const double far_y = layout.rows.y_for_row(3) + 200.0;
    check_size(app::select_from_gantt(sheet, fixture.map, layout, far_x, far_y, kNameColumn),
               core::sheet::kNoIndex, "a click on empty space is a miss");
    check_size(sheet.anchor().row, before, "a miss does not move the selection");

    // A milestone bar is hit-tested through the same rect.
    const core::gantt::Bar* milestone = layout.bar(kMilestoneUid);
    check(milestone != nullptr, "the milestone has a bar");
    if (milestone != nullptr) {
        check_size(app::select_from_gantt(sheet, fixture.map, layout, milestone->rect.center_x(),
                                          milestone->rect.center_y(), kNameColumn),
                   3, "clicking the milestone selects its row");
    }
}

void test_gantt_link_click() {
    // Two tasks far apart in time and a link between them: the routed elbow runs
    // between the rows, outside both bars, so a click on the lane can only be a
    // link hit.
    app::PlanInput input;
    input.title = "Link fixture";
    input.project_start = from_ymd(2026, 1, 1, 8, 0);
    input.tasks.push_back(make_task(10, 1, "1", "Predecessor", 2 * 4800, from_ymd(2026, 1, 1, 8, 0),
                                    from_ymd(2026, 1, 2, 17, 0), 0, 0, false, false, false));
    input.tasks.push_back(make_task(11, 2, "2", "Successor", 2 * 4800, from_ymd(2026, 2, 1, 8, 0),
                                    from_ymd(2026, 2, 2, 17, 0), 0, 0, false, false, false));
    input.links.push_back(make_link(10, 11, core::gantt::LinkType::finish_to_start));
    Fixture fixture(input);
    core::sheet::SheetModel& sheet = *fixture.sheet;

    check_size(fixture.gantt.links.size(), 1, "the link is routed");
    if (fixture.gantt.links.empty()) {
        return;
    }
    const core::gantt::LinkRoute& route = fixture.gantt.links[0];
    // The elbow's middle horizontal segment: two consecutive points with the
    // same y.  Click its midpoint, halfway between the row centres.
    double lane_y = 0.0;
    double lane_x = 0.0;
    bool found = false;
    for (std::size_t index = 1; index < route.points.size(); ++index) {
        if (std::abs(route.points[index - 1].y - route.points[index].y) < 1e-9 &&
            std::abs(route.points[index - 1].x - route.points[index].x) > 20.0) {
            lane_y = route.points[index].y;
            lane_x = (route.points[index - 1].x + route.points[index].x) / 2.0;
            found = true;
            break;
        }
    }
    check(found, "the route has a horizontal lane segment");
    if (!found) {
        return;
    }
    const std::size_t row = app::select_from_gantt(sheet, fixture.map, fixture.gantt, lane_x,
                                                   lane_y, kNameColumn);
    check_size(row, 1, "clicking a dependency selects the successor row");
    check(app::selected_task(sheet, fixture.map).uid == 11, "the successor uid is selected");
}

void test_collapse() {
    const app::PlanInput input = synthetic_plan();
    Fixture fixture(input);
    core::sheet::SheetModel& sheet = *fixture.sheet;

    check(app::select_cell(sheet, 2, kNameColumn), "select a child before collapsing");
    check(app::toggle_row(sheet, 0), "collapsing the summary succeeds");
    check_size(sheet.visible_row_count(), 1, "one visible row after collapse");

    fixture.rebuild(input);
    check_size(fixture.map.size(), 1, "the map follows the collapse");
    check_size(fixture.map.row_at(0), 0, "the summary is the only gantt row");
    check_size(fixture.map.ordinal_of(2), core::sheet::kNoIndex, "the hidden child has no ordinal");
    check_int(fixture.gantt.row_count, 1, "the gantt has one row");
    check_size(fixture.gantt.bars.size(), 1, "only the summary keeps a bar");
    check_size(fixture.gantt.links.size(), 0, "links to hidden rows are dropped");

    // The model clamped the selection onto the surviving summary row.
    const app::SelectedTask selected = app::selected_task(sheet, fixture.map);
    check(selected.has_selection, "the selection survives the collapse");
    check_size(selected.row, 0, "the selection clamps to the summary");
    check(selected.uid == kSummaryUid, "the clamped selection is the summary's uid");
    check_size(app::row_after(fixture.map, 0, 1), 0, "navigation clamps to the visible list");

    // Expanding restores every row and the bar count.
    check(app::toggle_row(sheet, 0), "expanding the summary succeeds");
    fixture.rebuild(input);
    check_size(sheet.visible_row_count(), 4, "all rows are visible again");
    check_int(fixture.gantt.row_count, 4, "the gantt has four rows again");
    check_size(fixture.gantt.bars.size(), 4, "every task has a bar again");
    check_size(fixture.gantt.links.size(), 2, "both links are routed again");

    // A non-summary row is not collapsible.
    check(!app::toggle_row(sheet, 1), "a task row cannot be collapsed");
    check_size(sheet.visible_row_count(), 4, "a refused collapse changes nothing");
}

void test_edit_and_undo() {
    app::PlanInput input = synthetic_plan();
    Fixture fixture(input);
    core::sheet::SheetModel& sheet = *fixture.sheet;

    // The host's edit policy: identity columns refuse an edit session.
    check(!sheet.begin_edit(1, kUidColumn), "UID is read-only");
    check(!sheet.begin_edit(1, kIdColumn), "ID is read-only");
    check(!sheet.begin_edit(1, kOutlineColumn), "Outline number is read-only");
    check(sheet.column_read_only(kUidColumn), "UID column reports read-only");
    check(!sheet.column_read_only(kNameColumn), "Name column is editable");
    check(!sheet.column_read_only(kStartColumn), "Start column is editable");

    // Typing a date: begin_edit seeds the cell text, commit writes through.
    check(sheet.begin_edit(2, kStartColumn), "begin_edit on Start");
    check(sheet.editing(), "the model is in an edit session");
    check(!sheet.edit_text().empty(), "the edit session is seeded with the cell text");
    sheet.set_edit_text("2026-11-02 08:00");
    const core::sheet::EditOutcome outcome = sheet.commit_edit(false);
    check(outcome.ok, "the new start commits");
    check(outcome.changed, "the commit changed the value");
    check(!sheet.editing(), "the edit session closed on commit");

    // The Gantt follows the edit: the synced plan carries the new start.
    app::PlanInput view = app::sync_plan_from_records(input, fixture.records);
    check_date(view.tasks[2].start, from_ymd(2026, 11, 2, 8, 0), "synced plan has the new start");
    check_date(input.tasks[2].start, from_ymd(2026, 10, 22, 8, 0),
               "the loaded baseline is unchanged");

    // The save diff sees exactly that row.
    std::vector<app::StartEdit> edits = app::collect_start_edits(fixture.records, input);
    check_size(edits.size(), 1, "one start differs from the loaded plan");
    if (!edits.empty()) {
        check(edits[0].uid == kSecondChildUid, "the edited row is the second child");
        check_date(edits[0].start, from_ymd(2026, 11, 2, 8, 0), "the diff carries the new date");
    }

    // A rejected commit keeps the session open and shows why.
    check(sheet.begin_edit(3, kStartColumn), "begin_edit on the milestone start");
    sheet.set_edit_text("not a date");
    const core::sheet::EditOutcome rejected = sheet.commit_edit(false);
    check(!rejected.ok, "a bad date is rejected");
    check(!rejected.error.empty(), "the rejection carries a message");
    check(sheet.editing(), "a rejected commit leaves the session open");
    sheet.cancel_edit();
    check(!sheet.editing(), "cancel closes the session");

    // Undo restores the loaded value; the save diff goes empty; redo re-applies.
    check(sheet.can_undo(), "the commit is on the undo stack");
    check(sheet.undo(), "undo succeeds");
    check(app::collect_start_edits(fixture.records, input).empty(),
          "undo restores the loaded start, so there is nothing to save");
    check(sheet.can_redo(), "the undo left a redo step");
    check(sheet.redo(), "redo succeeds");
    check_size(app::collect_start_edits(fixture.records, input).size(), 1,
               "redo brings the edit back");

    // accept_start_edits makes the saved value the new baseline.
    app::accept_start_edits(input, app::collect_start_edits(fixture.records, input));
    check(app::collect_start_edits(fixture.records, input).empty(),
          "after a save the baseline follows the records");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_row_map();
    test_selection_and_scroll();
    test_gantt_click();
    test_gantt_link_click();
    test_collapse();
    test_edit_and_undo();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("selection_model_tests: %d checks passed\n", g_checks);
    return 0;
}
