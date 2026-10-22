// app/document_tests.cpp -- the save helper, proved headlessly.
//
// Two layers:
//   1. the pure parts -- default_save_name() and collect_start_edits() -- on
//      synthetic rows, always;
//   2. the real write: take the committed synthetic fixture
//      (tests/data/synthetic-base.mpp, NOT client data -- see tests/data/
//      README.md), edit one task's Start through the sheet model exactly as the
//      shell's keyboard does, write the edit set to a file under the build tree,
//      then RE-READ that file with the .mpp reader and check the pin landed.
//
// The source fixture is never written to: the helper refuses target == source,
// and the test asserts the fixture's size is unchanged afterwards.
//
// What this cannot prove: that MS Project itself honours the written pin when it
// opens the file.  That was measured through COM in W29 (memory/writer.md:
// `pin_task_start` moved uid 1 and Project's recalc kept it) and is not repeated
// here -- there is no Project on this box and none on the runner.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w38 -G "Visual Studio 17 2022" -A x64
//   cmake --build build\vs-w38 --config Release
//   ctest --test-dir build\vs-w38 -C Release -R "^app_document$" --output-on-failure

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "date.h"
#include "document.h"
#include "layout_model.h"
#include "reader_adapter.h"
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

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
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

// Not called file_size(): that name would collide with std::filesystem::file_size
// through argument-dependent lookup on a path argument.
std::size_t bytes_on_disk(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

app::TaskRow make_task(std::uint32_t uid, date32 start) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.name = "Task";
    row.duration_ticks = 4800;
    row.start = start;
    row.finish = start;
    return row;
}

// ---------------------------------------------------------------------------
// The pure parts
// ---------------------------------------------------------------------------

void test_default_save_name() {
    check_text(app::default_save_name("C:/projects/plan.mpp"), "plan-edited.mpp",
               "save name replaces the extension");
    check_text(app::default_save_name("/home/me/Programme 2026.MPP"), "Programme 2026-edited.mpp",
               "save name keeps spaces and the stem");
    check_text(app::default_save_name("no-extension"), "no-extension-edited.mpp",
               "save name works without an extension");
    check_text(app::default_save_name(""), "project-edited.mpp",
               "an empty path still gives a usable name");
    check_text(app::default_save_name("archive.tar.gz"), "archive.tar-edited.mpp",
               "only the last extension is replaced");
}

void test_collect_start_edits() {
    app::PlanInput loaded;
    loaded.tasks.push_back(make_task(1, from_ymd(2026, 1, 5, 8, 0)));
    loaded.tasks.push_back(make_task(2, from_ymd(2026, 1, 6, 8, 0)));

    core::view::RecordSet records = app::make_record_set(loaded);
    check(app::collect_start_edits(records, loaded).empty(), "identical records need no save");

    const core::view::FieldId start_field = app::task_field_id("Start");
    check(start_field != 0, "Task.Start resolved");

    // One changed start, written straight into the RecordSet (what commit_edit
    // does).
    core::view::DateValue value = app::date_value_from_date32(from_ymd(2026, 2, 2, 8, 0));
    records.set_field(1, start_field, core::view::Value::of_date(value));
    const std::vector<app::StartEdit> edits = app::collect_start_edits(records, loaded);
    check_size(edits.size(), 1, "one changed start is collected");
    if (!edits.empty()) {
        check(edits[0].uid == 2, "the edit carries the row's uid");
        check_date(edits[0].start, from_ymd(2026, 2, 2, 8, 0), "the edit carries the new date");
    }

    // Clearing the cell is a change with no date: collected, and reported by the
    // save rather than silently dropped.
    records.set_field(1, start_field, core::view::Value{});
    const std::vector<app::StartEdit> cleared = app::collect_start_edits(records, loaded);
    check_size(cleared.size(), 1, "a cleared start is collected");
    if (!cleared.empty()) {
        check_date(cleared[0].start, kDateNa, "a cleared start is NA");
    }

    // accept_start_edits moves the baseline, so a second save is a no-op.
    app::accept_start_edits(loaded, edits);
    check(app::collect_start_edits(records, loaded).size() == 1,
          "the cleared value still differs from the (baseline) date");
}

// ---------------------------------------------------------------------------
// The real write
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)

struct Plan {
    app::PlanInput loaded;
    core::view::RecordSet records{core::model::RecordType::Task};
    std::unique_ptr<core::sheet::SheetModel> sheet;
};

bool load_plan(const std::filesystem::path& path, Plan& plan) {
    try {
        const mpp::Document document = mpp::load_file(path.string());
        plan.loaded = app::plan_input_from_document(document);
        plan.records = app::make_record_set(plan.loaded);
        plan.sheet = std::make_unique<core::sheet::SheetModel>(plan.records,
                                                               app::default_sheet_columns());
        app::apply_edit_policy(*plan.sheet);
        return true;
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: cannot load %s: %s\n", path.string().c_str(), error.what());
        return false;
    }
}

void test_save_round_trip() {
    const std::filesystem::path source =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
    if (!std::filesystem::exists(source)) {
        std::printf("SKIP: %s is not present; the save round trip needs the committed fixture\n",
                    source.string().c_str());
        return;
    }
    const std::size_t source_bytes = bytes_on_disk(source);

    Plan plan;
    if (!load_plan(source, plan) || plan.sheet == nullptr || plan.loaded.tasks.empty()) {
        return;
    }
    check(plan.loaded.tasks.size() >= 1, "the fixture has tasks");
    const std::uint32_t uid = plan.loaded.tasks[0].uid;
    const date32 new_start = from_ymd(2026, 3, 2, 8, 0);

    // Edit the first task's Start through the sheet, as the keyboard does.
    check(plan.sheet->begin_edit(0, 4), "begin_edit on the first task's Start");
    plan.sheet->set_edit_text("2026-03-02 08:00");
    const core::sheet::EditOutcome outcome = plan.sheet->commit_edit(false);
    check(outcome.ok, "the edited date is accepted");
    check(outcome.changed, "the edited date changes the value");

    const std::vector<app::StartEdit> edits = app::collect_start_edits(plan.records, plan.loaded);
    check_size(edits.size(), 1, "exactly one start is pending");
    if (!edits.empty()) {
        check(edits[0].uid == uid, "the pending edit is the edited task");
        check_date(edits[0].start, new_start, "the pending edit is the new date");
    }

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    const std::filesystem::path target =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-document-test.mpp";
    std::filesystem::remove(target, error);

    // Refusing to overwrite the opened file is part of the contract.
    const app::SaveOutcome refused = app::save_start_edits(edits, source.string(), source.string());
    check(!refused.ok, "saving over the opened file is refused");
    check(refused.error.find("refusing") != std::string::npos, "the refusal explains itself");
    check_size(bytes_on_disk(source), source_bytes, "the source is untouched by the refusal");

    const app::SaveOutcome saved = app::save_start_edits(edits, source.string(), target.string());
    check(saved.ok, "the save reports success");
    check(saved.error.empty(), "a successful save has no error");
    check_size(saved.requested, 1, "the save was asked for one row");
    check_size(saved.rows_written, 1, "the save wrote one row");
    check_text(saved.path, target.string(), "the outcome names the target");
    check(!saved.api.empty(), "the outcome names the writer path");
    std::printf("SAVE: %s\n", saved.api.c_str());
    for (const std::string& warning : saved.warnings) {
        std::printf("SAVE WARNING: %s\n", warning.c_str());
    }
    for (const std::string& note : saved.notes) {
        std::printf("SAVE NOTE: %s\n", note.c_str());
    }
    check_size(bytes_on_disk(source), source_bytes, "the source is untouched by the save");
    check(std::filesystem::exists(target), "the target file exists");

    // Re-read the written file with the reader: it must still be a .mpp, with the
    // same task count, and (through the documented writer path) the edited task's
    // start pinned by an SNET constraint.
    try {
        const mpp::Document reread = mpp::load_file(target.string());
        check_size(reread.tasks.size(), plan.loaded.tasks.size(), "the saved file has every task");
        const mpp::Task* task = nullptr;
        for (const mpp::Task& candidate : reread.tasks) {
            if (candidate.uid == uid) {
                task = &candidate;
                break;
            }
        }
        check(task != nullptr, "the edited task is in the saved file");
        if (task != nullptr) {
            // The reader's Effective Start (+104) is what Project recalculates on
            // open, so it is not required to move here; the constraint is what
            // makes it move.  W29 measured Project honouring exactly this.
#if !APP_MPPWRITE_SAVE_HEADER
            check_size(task->constraint_type, 4, "the start is pinned by a SNET constraint");
            check_size(task->constraint_date.raw, app::packed_from_date32(new_start),
                       "the constraint date is the edited start");
#else
            std::printf("NOTE: mppwrite::save_date_edits owns the encoding; "
                        "constraint %u date %s\n",
                        static_cast<unsigned>(task->constraint_type),
                        mpp::format_date(task->constraint_date).c_str());
#endif
        }
    } catch (const std::exception& reread_error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: the saved file does not load: %s\n", reread_error.what());
    }

    // A cleared start has no date to write: reported, nothing written, ok.
    Plan cleared_plan;
    if (load_plan(source, cleared_plan) && cleared_plan.sheet != nullptr) {
        check(cleared_plan.sheet->begin_edit(0, 4), "begin_edit before clearing");
        cleared_plan.sheet->set_edit_text("");
        const core::sheet::EditOutcome cleared = cleared_plan.sheet->commit_edit(false);
        check(cleared.ok, "an empty date clears the cell");
        const std::vector<app::StartEdit> cleared_edits =
            app::collect_start_edits(cleared_plan.records, cleared_plan.loaded);
        check_size(cleared_edits.size(), 1, "the cleared start is a pending change");
        const std::filesystem::path cleared_target =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-document-cleared.mpp";
        std::filesystem::remove(cleared_target, error);
        const app::SaveOutcome cleared_outcome =
            app::save_start_edits(cleared_edits, source.string(), cleared_target.string());
        check(cleared_outcome.ok, "a cleared start is not an error");
        check_size(cleared_outcome.rows_written, 0, "a cleared start writes no row");
        check(!cleared_outcome.warnings.empty(), "a cleared start is reported");
        check(!std::filesystem::exists(cleared_target), "nothing is written when there is no date");
    }

    // An edit for a uid that is not in the file fails cleanly (no output file).
    if (!edits.empty()) {
        std::vector<app::StartEdit> bogus;
        bogus.push_back(app::StartEdit{999999u, new_start});
        const std::filesystem::path bogus_target =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-document-bogus.mpp";
        std::filesystem::remove(bogus_target, error);
        const app::SaveOutcome bogus_outcome =
            app::save_start_edits(bogus, source.string(), bogus_target.string());
        check(!bogus_outcome.ok, "an unknown uid is an error");
        check(!bogus_outcome.error.empty(), "the error explains itself");
        check(!std::filesystem::exists(bogus_target), "a failed save leaves no file");
    }
}

#else
void test_save_round_trip() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
}
#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_default_save_name();
    test_collect_start_edits();
    test_save_round_trip();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("document_tests: %d checks passed\n", g_checks);
    return 0;
}
