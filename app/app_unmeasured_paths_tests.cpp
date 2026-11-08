// app/app_unmeasured_paths_tests.cpp -- W34-4.0: the five paths whose save does
// not match Project's are refused up front.
//
// W34-0.5 measured Insert Task, Delete Task, link ADD and the custom-field SET
// and CLEAR against Project on the synthetic base; the port's save differs from
// Project's in 15, 6, 13, 2 and 5 streams (the named KnownGap tables in
// tests/roundtrip_add_delete.cpp, roundtrip_links.cpp and
// roundtrip_field_values.cpp).  Standing rule 1 therefore refuses each path with
// the decision-9 reason.  This program NEVER lifts the refusal: it is the proof
// that the default is "refused", one test per path, for the builder the shells
// call, the queue (StructuralEdits::push) and the save cascade.
//
// Build/run (Windows, MSVC; see app/README.md):
//   ctest --test-dir build\vs-w55 -C Release -R "^app_unmeasured_paths$" --output-on-failure

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "date.h"
#include "edit_model.h"
#include "field_link_edits.h"
#include "layout_model.h"
#include "reader_adapter.h"

namespace {

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

bool is_decision9(const std::string& text) {
    return text.find("is not implemented in this port") != std::string::npos &&
           text.find("differs from Project's") != std::string::npos;
}

app::TaskRow make_task(std::uint32_t uid) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.file_row = uid;
    row.name = "Task";
    row.duration_ticks = 4800;
    row.start = from_ymd(2026, 1, 4 + static_cast<int>(uid), 8, 0);
    row.finish = row.start;
    return row;
}

app::PlanInput make_plan() {
    app::PlanInput plan;
    plan.tasks.push_back(make_task(1));
    plan.tasks.push_back(make_task(2));
    plan.tasks.push_back(make_task(3));
    return plan;
}

const std::uint32_t kText1 = mpp::custom_field_id(mpp::FieldKind::text, 1);

app::TaskRow make_row_with_text() {
    app::TaskRow row = make_task(7);
    mpp::CustomFieldValue text;
    text.field_id = kText1;
    text.kind = mpp::FieldKind::text;
    text.present = true;
    text.storage_measured = true;
    text.text = "alpha";
    row.custom_fields = {text};
    return row;
}

// The gate is on by default: no test program lifts it here.
void test_default_is_refused() {
    check(!app::unmeasured_paths_allowed_for_tests(), "the refusal is on by default");
}

void test_insert_task_refused() {
    const app::PlanInput plan = make_plan();
    std::string error;
    check(!app::check_insert_task(plan, {}, error), "Insert Task is refused up front");
    check(is_decision9(error), "the Insert Task refusal carries the decision-9 reason");
    check(error.find("Insert Task") != std::string::npos && error.find("15 stream") != std::string::npos,
          "the Insert Task refusal names the command and the 15 differing streams");
    app::StructuralEdits edits;
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::insert_task;
    edit.name = "New";
    edit.uid = 99;
    edit.anchor_uid = 1;
    check(!edits.push(edit), "the queue refuses a hand-built insert (no plan)");
    check(!edits.push(edit, &plan), "the queue refuses a hand-built insert (with a plan)");
    check(edits.empty(), "a refused insert queues nothing");
}

void test_delete_task_refused() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdit edit;
    std::string error;
    check(!app::make_delete_edit(plan, 2, edit, error), "Delete Task is refused up front");
    check(is_decision9(error), "the Delete Task refusal carries the decision-9 reason");
    check(error.find("Delete Task") != std::string::npos && error.find("6 stream") != std::string::npos,
          "the Delete Task refusal names the command and the 6 differing streams");
    app::StructuralEdits edits;
    app::StructuralEdit hand_built;
    hand_built.kind = app::StructuralKind::delete_task;
    hand_built.uid = 2;
    check(!edits.push(hand_built, &plan), "the queue refuses a hand-built delete");
    check(edits.empty(), "a refused delete queues nothing");
}

void test_link_add_refused() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    const std::string reason = app::validate_link_add(plan, edits, 1, 2,
                                                      core::gantt::LinkType::finish_to_start);
    check(is_decision9(reason), "the link ADD validation carries the decision-9 reason");
    check(reason.find("13 stream") != std::string::npos,
          "the link ADD refusal names the 13 differing streams");
    app::StructuralEdit out;
    std::string error;
    check(!app::make_link_add(plan, edits, 1, 2, core::gantt::LinkType::finish_to_start, "",
                              out, error),
          "the link ADD is refused up front");
    check(is_decision9(error), "make_link_add reports the decision-9 reason");
    app::StructuralEdit hand_built;
    hand_built.kind = app::StructuralKind::add_link;
    hand_built.uid = 2;
    hand_built.predecessor_uid = 1;
    check(!edits.push(hand_built, &plan), "the queue refuses a hand-built link add");
    check(edits.empty(), "a refused link add queues nothing");
}

void test_field_set_refused() {
    const app::TaskRow row = make_row_with_text();
    std::vector<app::TaskFieldInput> inputs;
    inputs.push_back(app::TaskFieldInput{kText1, "beta"});
    std::vector<app::StructuralEdit> queued;
    std::string error;
    check(!app::make_task_field_edits(row, inputs, core::sheet::SheetFormat{}, queued, error),
          "a custom-field SET is refused up front");
    check(is_decision9(error), "the SET refusal carries the decision-9 reason");
    check(error.find("2 stream") != std::string::npos,
          "the SET refusal names the 2 differing streams");
    check(queued.empty(), "a refused form queues nothing");
    app::StructuralEdits edits;
    app::StructuralEdit hand_built;
    hand_built.kind = app::StructuralKind::set_field;
    hand_built.uid = 7;
    hand_built.field_id = kText1;
    check(!edits.push(hand_built), "the queue refuses a hand-built SET");
    check(edits.empty(), "a refused SET queues nothing");
}

void test_field_clear_refused() {
    const app::TaskRow row = make_row_with_text();
    std::vector<app::TaskFieldInput> inputs;
    inputs.push_back(app::TaskFieldInput{kText1, ""});
    std::vector<app::StructuralEdit> queued;
    std::string error;
    check(!app::make_task_field_edits(row, inputs, core::sheet::SheetFormat{}, queued, error),
          "a custom-field CLEAR is refused up front");
    check(is_decision9(error), "the CLEAR refusal carries the decision-9 reason");
    check(error.find("5 stream") != std::string::npos,
          "the CLEAR refusal names the 5 differing streams");
    check(queued.empty(), "a refused form queues nothing");
    app::StructuralEdits edits;
    app::StructuralEdit hand_built;
    hand_built.kind = app::StructuralKind::clear_field;
    hand_built.uid = 7;
    hand_built.field_id = kText1;
    check(!edits.push(hand_built), "the queue refuses a hand-built CLEAR");
    check(edits.empty(), "a refused CLEAR queues nothing");
}

// Every other kind is outside the gate (the measured shapes stay enabled).
void test_other_kinds_untouched() {
    const app::StructuralKind others[] = {
        app::StructuralKind::delete_link,       app::StructuralKind::set_percent,
        app::StructuralKind::set_project_priority, app::StructuralKind::update_actuals,
        app::StructuralKind::outline_indent,    app::StructuralKind::outline_outdent,
        app::StructuralKind::set_new_task_mode, app::StructuralKind::set_task_mode,
    };
    for (const app::StructuralKind kind : others) {
        check(app::unmeasured_path_refusal(kind).empty(),
              "a measured kind is not refused by the W34-4.0 gate");
    }
    check(!app::unmeasured_path_refusal(app::StructuralKind::insert_task).empty() &&
              !app::unmeasured_path_refusal(app::StructuralKind::delete_task).empty() &&
              !app::unmeasured_path_refusal(app::StructuralKind::add_link).empty() &&
              !app::unmeasured_path_refusal(app::StructuralKind::set_field).empty() &&
              !app::unmeasured_path_refusal(app::StructuralKind::clear_field).empty(),
          "exactly the five unmeasured kinds are refused");
}

// A queue built while the gate was lifted (a test program's) still cannot be
// saved once the gate is back on: the cascade repeats the refusal before the
// first write, so no file appears.
void test_cascade_refuses() {
    const std::filesystem::path out_dir =
        std::filesystem::path(APP_TEST_OUT_DIR) / "unmeasured_paths";
    std::error_code ignore;
    std::filesystem::create_directories(out_dir, ignore);
    const std::filesystem::path target = out_dir / "never-written.mpp";
    std::filesystem::remove(target, ignore);

    const app::PlanInput plan = make_plan();
    app::allow_unmeasured_paths_for_tests(true);
    app::StructuralEdits edits;
    app::StructuralEdit hand_built;
    hand_built.kind = app::StructuralKind::delete_task;
    hand_built.uid = 2;
    const bool queued = edits.push(hand_built, &plan);
    app::allow_unmeasured_paths_for_tests(false);
    check(queued, "the queue accepts the delete while the gate is lifted (test setup)");

    const std::filesystem::path source =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
    const app::StructuralSaveResult result =
        app::save_structural_edits(edits, {}, source.string(), target.string(), false);
    check(!result.ok, "the cascade refuses a queued delete once the gate is back");
    check(is_decision9(result.error), "the cascade reports the decision-9 reason");
    check(!std::filesystem::exists(target), "the refused cascade wrote no file");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_default_is_refused();
    test_insert_task_refused();
    test_delete_task_refused();
    test_link_add_refused();
    test_field_set_refused();
    test_field_clear_refused();
    test_other_kinds_untouched();
    test_cascade_refuses();
    std::printf("%s: %d checks, %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
