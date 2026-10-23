// app/export_tests.cpp -- Export PDF, proved headlessly.
//
// app/export_pdf.cpp calls core/render (stream W39) to write a real PDF from the
// same models the window draws.  This test writes those PDFs under
// APP_TEST_OUT_DIR (never into the source tree), then checks the bytes: the
// header, the trailer, the page count the renderer reports, the filtered row
// count, the determinism of two identical exports, and that a duration
// preference of "hours" reaches the page as text.
//
// Synthetic rows only: no corpus, no client data.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "calendar.h"
#include "date.h"
#include "export_pdf.h"
#include "filter_model.h"
#include "gantt.h"
#include "layout_model.h"
#include "prefs.h"
#include "sheet.h"
#include "value.h"

namespace {

using app::ExportRequest;
using app::ExportOutcome;
using app::Preferences;
using app::ReportKind;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
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

std::string out_path(const char* name) {
    return (std::filesystem::path(APP_TEST_OUT_DIR) / name).string();
}

std::string read_file(const std::string& path) {
    // ifstream rather than fopen: MSVC deprecates the CRT family under /W4 /WX,
    // and this test must build with the same flags as the shell.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::string data;
    char buffer[8192];
    while (file.read(buffer, static_cast<std::streamsize>(sizeof(buffer))) || file.gcount() > 0) {
        data.append(buffer, static_cast<std::size_t>(file.gcount()));
    }
    return data;
}

app::PlanInput make_plan() {
    app::PlanInput input;
    input.title = "Synthetic plan";
    input.project_start = core::sched::from_ymd(2026, 10, 19);
    const char* names[] = {"One", "Two", "Three", "Four"};
    for (std::size_t index = 0; index < 4; ++index) {
        app::TaskRow row;
        row.uid = static_cast<std::uint32_t>(index + 1);
        row.id = static_cast<std::uint32_t>(index + 1);
        row.outline_number = std::to_string(index + 1);
        row.name = names[index];
        row.duration_ticks = static_cast<std::int64_t>(index + 1) * core::view::kTicksPerDay;
        row.start = core::sched::from_ymd(2026, 10, 19 + static_cast<int>(index));
        row.finish = row.start;
        input.tasks.push_back(row);
    }
    app::LinkRow link;
    link.predecessor_uid = 1;
    link.successor_uid = 2;
    input.links.push_back(link);
    return input;
}

struct Fixture {
    app::PlanInput plan = make_plan();
    core::view::RecordSet records = app::make_record_set(plan);
    core::sheet::SheetFormat format{};
    core::sheet::SheetMetrics metrics = app::default_sheet_metrics();
    Preferences prefs{};
    std::vector<core::gantt::LinkInput> links;

    Fixture() {
        app::apply_preferences(prefs, format);
        for (const app::LinkRow& row : plan.links) {
            core::gantt::LinkInput link;
            link.predecessor_uid = row.predecessor_uid;
            link.successor_uid = row.successor_uid;
            link.type = row.type;
            links.push_back(link);
        }
    }

    ExportRequest request(ReportKind kind) const {
        ExportRequest request;
        request.kind = kind;
        request.records = &records;
        request.columns = app::default_sheet_columns();
        request.format = format;
        request.metrics = metrics;
        request.preferences = prefs;
        request.title = plan.title;
        request.subtitle = app::export_subtitle(kind, "", 4, 4);
        request.footer = app::export_footer(prefs, "Two");
        request.links = links;
        request.timescale = app::timescale_for_zoom(plan, core::gantt::GanttZoom::week);
        request.auto_timescale = true;
        request.working_day_shading = true;
        return request;
    }
};

void check_pdf(const ExportOutcome& outcome, const std::string& path, const char* what) {
    check(outcome.ok, what);
    if (!outcome.ok) {
        std::printf("  error: %s\n", outcome.error.c_str());
        return;
    }
    const std::string bytes = read_file(path);
    check(bytes.size() > 1000, "the PDF is not empty");
    check(bytes.rfind("%PDF-", 0) == 0, "the file starts with a PDF header");
    check(bytes.find("%%EOF") != std::string::npos, "the file has a PDF trailer");
    check(outcome.pages >= 1, "at least one page was produced");
    check_text(outcome.path, path, "the outcome reports the path it wrote");
}

void test_sheet_and_gantt() {
    const Fixture fixture;
    const std::string sheet_path = out_path("app-export-sheet.pdf");
    const ExportOutcome sheet =
        app::export_report(fixture.request(ReportKind::sheet), sheet_path);
    check_pdf(sheet, sheet_path, "the sheet report exports");
    check_size(sheet.rows, 4, "the sheet report prints every row");

    const std::string gantt_path = out_path("app-export-gantt.pdf");
    const ExportOutcome gantt =
        app::export_report(fixture.request(ReportKind::gantt), gantt_path);
    check_pdf(gantt, gantt_path, "the Gantt report exports");
    check_size(gantt.rows, 4, "the Gantt report prints every row");

    // Same request, same bytes: core/render is clock-free and this proves the
    // app side did not sneak anything non-deterministic in.
    const std::string again_path = out_path("app-export-sheet-again.pdf");
    const ExportOutcome again =
        app::export_report(fixture.request(ReportKind::sheet), again_path);
    check(again.ok, "the second sheet export succeeds");
    check(read_file(sheet_path) == read_file(again_path),
          "two identical exports produce identical bytes");
}

void test_filtered_rows() {
    const Fixture fixture;
    ExportRequest request = fixture.request(ReportKind::sheet);
    request.rows = {1, 2};
    const std::string path = out_path("app-export-filtered.pdf");
    const ExportOutcome outcome = app::export_report(request, path);
    check_pdf(outcome, path, "a filtered report exports");
    check_size(outcome.rows, 2, "only the requested rows are printed");

    // The filter's own description reaches the page through the subtitle (the
    // renderer draws it in the title block; the bytes carry it).
    ExportRequest described = fixture.request(ReportKind::sheet);
    described.rows = {1};
    described.subtitle = app::export_subtitle(
        ReportKind::sheet, "Name equals \"Two\"", 1, 4);
    const std::string described_path = out_path("app-export-described.pdf");
    const ExportOutcome described_outcome = app::export_report(described, described_path);
    check_pdf(described_outcome, described_path, "a described report exports");
    check_size(described_outcome.rows, 1, "the described report prints one row");
}

void test_duration_preference() {
    Fixture fixture;
    fixture.prefs.duration_style = app::DurationStyle::hours;
    ExportRequest request = fixture.request(ReportKind::sheet);
    request.preferences = fixture.prefs;
    const std::string path = out_path("app-export-hours.pdf");
    const ExportOutcome outcome = app::export_report(request, path);
    check_pdf(outcome, path, "a report in hours exports");
    const std::string bytes = read_file(path);
    // The substituted Duration column carries the app's own text: the first task
    // is one working day, i.e. 8 hrs.
    check(bytes.find("8 hrs") != std::string::npos,
          "the hours duration style reaches the printed page");
}

void test_helpers() {
    Preferences prefs;
    check_text(app::export_subtitle(ReportKind::sheet, "", 4, 4),
               "Sheet report - 4 of 4 tasks", "the sheet subtitle counts the rows");
    check_text(app::export_subtitle(ReportKind::gantt, "Name equals \"Two\"", 1, 4),
               "Gantt report - 1 of 4 tasks  |  filter: Name equals \"Two\"",
               "the Gantt subtitle carries the filter");
    check_text(app::export_subtitle(ReportKind::sheet, "", 1, 1),
               "Sheet report - 1 of 1 task", "a single task is singular");

    // Wave 25 (W25-11/S14): uid 0 is the project-summary row, not a task.
    core::view::RecordSet records(core::model::RecordType::Task);
    records.add_row(0);  // the project row
    records.add_row(5);
    records.add_row(9);
    check_size(app::task_row_count(records), 2,
               "the PDF task count excludes the project-summary row");
    check_size(app::task_row_count(records, {0, 1}), 1,
               "a filtered row list is counted the same way");
    check_size(app::task_row_count(records, {0}), 0,
               "the project row alone is no task");
    check_size(app::task_row_count(records, {}), 0,
               "an active filter that matches nothing is 0 of N, not all rows");
    // Wave 25 review: the BODY must agree with the subtitle.
    app::ExportRequest request;
    request.records = &records;
    check_size(app::export_rows(request).size(), 3,
               "no filter prints every RecordSet row");
    request.rows_are_filtered = true;
    request.rows.clear();
    check_size(app::export_rows(request).size(), 0,
               "an active zero-match filter prints no rows");
    request.rows = {1};
    check_size(app::export_rows(request).size(), 1,
               "a filtered list prints exactly its rows");

    check_text(app::export_footer(prefs, ""), app::preferences_summary(prefs),
               "the footer without a selection is the preference summary");
    const std::string footer = app::export_footer(prefs, "Two");
    check(footer.rfind("Selected: Two", 0) == 0, "the footer names the selected task");
    check(footer.find("timescale day") != std::string::npos,
          "the footer carries the timescale preference");

    check_text(app::export_default_name("plan.mpp", ReportKind::sheet), "plan-sheet.pdf",
               "the sheet export name replaces the extension");
    check_text(app::export_default_name("plan.mpp", ReportKind::gantt), "plan-gantt.pdf",
               "the Gantt export name replaces the extension");
    check_text(app::export_default_name("", ReportKind::sheet), "project-sheet.pdf",
               "an empty source path still suggests a name");
    check_text(app::export_default_name("C:/tmp/plan.mpp", ReportKind::gantt),
               "plan-gantt.pdf", "a full path is reduced to its stem");
}

void test_shading_calendar() {
    check(app::shading_calendar(true) == nullptr,
          "shading on uses the layout's own weekend mask");
    const std::shared_ptr<const core::sched::Calendar> all_working =
        app::shading_calendar(false);
    check(all_working != nullptr, "shading off supplies an all-working calendar");
    if (all_working == nullptr) {
        return;
    }
    // 2026-10-24 is a Saturday (the synthetic plan's Monday is 2026-10-19).
    const core::sched::date32 saturday = core::sched::from_ymd(2026, 10, 24);
    check(all_working->is_working_day(core::sched::day_of(saturday)),
          "the shading-off calendar makes Saturday a working day");
    const core::sched::date32 sunday = core::sched::from_ymd(2026, 10, 25);
    check(all_working->is_working_day(core::sched::day_of(sunday)),
          "the shading-off calendar makes Sunday a working day");
    check(all_working->day_total_ticks(core::sched::day_of(saturday)) > 0,
          "the shading-off calendar has working time on Saturday");
}

void test_failures() {
    Fixture fixture;
    ExportRequest no_records = fixture.request(ReportKind::sheet);
    no_records.records = nullptr;
    const ExportOutcome no_records_outcome =
        app::export_report(no_records, out_path("app-export-nothing.pdf"));
    check(!no_records_outcome.ok, "a request with no records fails");
    check(!no_records_outcome.error.empty(), "the no-records failure has a message");

    const ExportOutcome no_path =
        app::export_report(fixture.request(ReportKind::sheet), "");
    check(!no_path.ok, "an empty path fails");
    check(!no_path.error.empty(), "the empty-path failure has a message");

    const ExportOutcome bad_dir = app::export_report(
        fixture.request(ReportKind::sheet), out_path("no-such-directory/plan.pdf"));
    check(!bad_dir.ok, "an unwritable directory fails");
    check(!bad_dir.error.empty(), "the unwritable-directory failure has a message");
}

}  // namespace

int main() {
    test_sheet_and_gantt();
    test_filtered_rows();
    test_duration_preference();
    test_helpers();
    test_shading_calendar();
    test_failures();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("export_tests: %d checks passed\n", g_checks);
    return 0;
}
