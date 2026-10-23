// app/project_dialogs_tests.cpp -- wave 26 G5's headless gate for the Project
// tab's dialog payloads.
//
// The rules this file enforces are the brief's: every value a dialog shows is a
// mapping from the engine, and every functional control ends in a WRITER whose
// effect is re-read from the file.  The one functional Project Information field
// is Priority; this test saves it through the shared StructuralEdits cascade and
// re-opens the written file to prove the project row's FixedData word changed.
//
// The fixture is copied under APP_TEST_OUT_DIR before it is saved from; nothing
// here writes tests/data/.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "edit_model.h"
#include "layout_model.h"
#include "project_dialogs.h"
#include "reader.h"
#include "reader_adapter.h"

namespace {

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

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

int main() {
    const std::string fixture = std::string(APP_TEST_DATA_DIR) + "/synthetic-base.mpp";
    const std::filesystem::path out_dir = APP_TEST_OUT_DIR;
    std::filesystem::create_directories(out_dir);

    mpp::Document document;
    try {
        document = mpp::load_file(fixture);
    } catch (const std::exception& error) {
        std::printf("FAIL: the fixture does not load: %s\n", error.what());
        return 1;
    }
    const app::PlanInput plan = app::plan_input_from_document(document);
    const core::sheet::SheetFormat format;
    const app::date32 today = core::sched::from_ymd(2026, 10, 23);

    // -----------------------------------------------------------------------
    // Project Information
    // -----------------------------------------------------------------------
    {
        const app::ProjectInformation info = app::project_information_from(
            plan, {"Standard", "Shift A"}, "Standard", today, format);
        check(info.has_start && info.has_finish, "the fixture stores project start and finish");
        check(contains(info.start_text, "04/01/26"),
              "the start date is rendered from the file");
        check(contains(info.finish_text, "09/01/26"),
              "the finish date is rendered from the file");
        check(info.priority == 500, "the project priority is the file's 500");
        check_text(info.schedule_from, "Project Start Date",
                   "Schedule from carries the captured default");
        check_text(info.status_text, "NA",
                   "the status date is NA: this port decodes no project status date");
        check(info.calendars.size() == 2 && info.calendars[1] == "Shift A",
              "the calendar list is the file's base-calendar names");
        check(info.calendar == "Standard" && info.has_calendar,
              "the project default calendar is shown");
        check(!info.title.empty(), "the project title is carried for the caption");
        std::fprintf(stderr, "note: Project Information start '%s' finish '%s' today '%s'\n",
                     info.start_text.c_str(), info.finish_text.c_str(),
                     info.current_text.c_str());
    }

    // An empty plan: the values are NA rather than invented.
    {
        app::PlanInput empty;
        empty.project_start = app::kDateNa;
        empty.project_finish = app::kDateNa;
        const app::ProjectInformation info =
            app::project_information_from(empty, {}, {}, today, format);
        check(!info.has_start && !info.has_finish && !info.has_calendar,
              "an empty plan has no project dates or calendar");
        check_text(info.start_text, "NA", "an empty start renders NA");
        check_text(info.finish_text, "NA", "an empty finish renders NA");
    }

    // -----------------------------------------------------------------------
    // The priority edit: the ONE functional Project Information field
    // -----------------------------------------------------------------------
    check(app::valid_project_priority(0) && app::valid_project_priority(1000),
          "Project's 0..1000 priority domain is accepted");
    check(!app::valid_project_priority(-1) && !app::valid_project_priority(1001),
          "a priority outside 0..1000 is refused");

    app::StructuralEdits edits;
    const app::StructuralEdit priority = app::make_project_priority_edit(750);
    check(priority.kind == app::StructuralKind::set_project_priority &&
              priority.uid == 0 && priority.priority == 750,
          "the priority edit addresses the project row with the chosen value");
    check(edits.push(priority), "the priority edit is accepted");
    check(edits.applied().size() == 1, "one pending priority edit");
    const app::PlanInput shown = edits.apply(plan);
    check(shown.project_priority == 750, "the display plan shows the chosen priority");
    check(edits.push(app::make_project_priority_edit(600)),
          "a second priority edit is accepted (last one wins at save)");
    check(edits.apply(plan).project_priority == 600, "the newest priority wins in the plan");

    {
        app::StructuralEdits invalid;
        app::StructuralEdit too_high = app::make_project_priority_edit(1000);
        too_high.priority = 1001;
        check(!invalid.push(too_high), "a priority above 1000 is refused");
        app::StructuralEdit wrong_row = app::make_project_priority_edit(500);
        wrong_row.uid = 7;
        check(!invalid.push(wrong_row), "a priority edit for a task row is refused");
    }

    // The real writer: save the priority and re-read it from the file.
    {
        const std::string source = (out_dir / "project-priority-source.mpp").string();
        const std::string target = (out_dir / "project-priority-saved.mpp").string();
        std::filesystem::copy_file(fixture, source,
                                   std::filesystem::copy_options::overwrite_existing);
        app::StructuralEdits save_edits;
        check(save_edits.push(app::make_project_priority_edit(750)),
              "the save set holds the priority edit");
        const app::StructuralSaveResult result = app::save_structural_edits(
            save_edits, {}, source, target, false);
        check(result.ok, "the priority save succeeded");
        if (!result.ok) {
            std::printf("  save error: %s\n", result.error.c_str());
        }
        check(result.priority_rows_written == 1, "one project priority row was written");
        const mpp::Document reread = mpp::load_file(target);
        check(reread.project.priority == 750,
              "the saved file re-reads the written project priority");
        const app::PlanInput reread_plan = app::plan_input_from_document(reread);
        check(reread_plan.project_priority == 750,
              "the re-read plan carries the saved project priority");
        std::fprintf(stderr, "note: saved priority %u to %s\n",
                     static_cast<unsigned>(reread.project.priority), target.c_str());
        std::filesystem::remove(source);
        std::filesystem::remove(target);
    }

    // -----------------------------------------------------------------------
    // Move Project's actual-values predicate
    // -----------------------------------------------------------------------
    {
        check(!app::project_has_actual_values(plan),
              "the synthetic fixture has no actual values (all 0 % complete)");
        app::PlanInput actual = plan;
        actual.tasks.front().percent_complete = 25;
        check(app::project_has_actual_values(actual),
              "a non-zero task percent complete is an actual value");
        app::PlanInput project_only = plan;
        project_only.project_percent_complete = 10;
        check(app::project_has_actual_values(project_only),
              "a non-zero project percent complete is an actual value");
    }

    std::printf("app_project_dialogs: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
