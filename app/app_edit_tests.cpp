// app/app_edit_tests.cpp -- Insert Task / Delete Task, proved headlessly (wave 12).
//
// Two layers:
//   1. the pending-edit MODEL (app/edit_model.h) on synthetic plans: what
//      apply() shows, where an inserted row lands, which dependent rows a delete
//      removes, the placeholder uid, push validation and undo/redo;
//   2. the real thing on a COPY of the committed synthetic fixture
//      (tests/data/synthetic-base.mpp, NOT client data -- see tests/data/
//      README.md) under APP_TEST_OUT_DIR: insert one task, save, re-read the file
//      and check the count and the uid the writer allocated; delete a task,
//      save, and check the count and that the cascade tombstoned its link rows;
//      a combined structural + start-date save; the refusals (no target, target ==
//      source without the app's own-file option, an unknown uid); and that the
//      fixture itself is never written.
//
// What this cannot prove: that MS Project reopens the written file with the same
// model.  That was measured through COM in wave 10/11 (w10-oracle §1,
// w11-oracle §2: add = 4 tasks, delete = 2 tasks and no dependants) and is not
// repeated here -- there is no Project on this box and none on the runner.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w51 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w51 --config Release
//   ctest --test-dir build\vs-w51 -C Release -R "^app_edit$" --output-on-failure

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "date.h"
#include "edit_model.h"
#include "layout_model.h"
#include "reader_adapter.h"

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

std::size_t bytes_on_disk(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

app::TaskRow make_task(std::uint32_t uid, std::int64_t duration_ticks, date32 start) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.name = "Task";
    row.duration_ticks = duration_ticks;
    row.start = start;
    row.finish = start;
    return row;
}

app::PlanInput make_plan() {
    app::PlanInput plan;
    plan.tasks.push_back(make_task(1, 4800, from_ymd(2026, 1, 5, 8, 0)));
    plan.tasks.push_back(make_task(2, 4800, from_ymd(2026, 1, 6, 8, 0)));
    plan.tasks.push_back(make_task(3, 4800, from_ymd(2026, 1, 7, 8, 0)));
    app::LinkRow first;
    first.predecessor_uid = 1;
    first.successor_uid = 2;
    app::LinkRow second;
    second.predecessor_uid = 2;
    second.successor_uid = 3;
    plan.links.push_back(first);
    plan.links.push_back(second);
    app::AssignmentRow assignment;
    assignment.uid = 10;
    assignment.task_uid = 2;
    assignment.resource_uid = 100;
    assignment.work_ticks = 4800;
    plan.assignments.push_back(assignment);
    return plan;
}

app::StructuralEdit insert_after(const std::string& name, std::uint32_t uid,
                                 std::uint32_t anchor) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::insert_task;
    edit.name = name;
    edit.uid = uid;
    edit.anchor_uid = anchor;
    return edit;
}

app::StructuralEdit delete_uid(std::uint32_t uid) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::delete_task;
    edit.uid = uid;
    return edit;
}

// ---------------------------------------------------------------------------
// The pending-edit model
// ---------------------------------------------------------------------------

void test_push_and_undo() {
    app::StructuralEdits edits;
    check(edits.empty(), "a fresh edit list is empty");
    check(!edits.can_undo() && !edits.can_redo(), "nothing to undo or redo");

    check(!edits.push(insert_after("", 4, 0)), "an empty name is refused");
    check(!edits.push(insert_after("   ", 4, 0)), "a whitespace-only name is refused");
    check(!edits.push(insert_after("New", 0, 0)), "uid 0 is refused");
    check(edits.empty(), "a refused push changes nothing");

    check(edits.push(insert_after("New Task", 4, 1)), "a valid insert is accepted");
    check(edits.size() == 1, "one edit");
    check(!edits.push(insert_after("Other", 4, 0)), "a duplicate placeholder uid is refused");
    check(edits.push(delete_uid(2)), "a delete is accepted");
    check(!edits.push(delete_uid(2)), "a duplicate delete is refused");
    check(edits.inserted_count() == 1 && edits.deleted_count() == 1, "one insert and one delete");

    check(edits.undo(), "undo moves the delete to the redo list");
    check(edits.can_redo(), "the redo list is not empty");
    check(edits.deleted_count() == 0, "the delete is no longer applied");
    check(edits.redo(), "redo applies it again");
    check(edits.deleted_count() == 1, "the delete is applied again");

    check(edits.push(delete_uid(3)), "a new edit clears the redo list");
    check(!edits.can_redo(), "the redo list is empty");

    check(edits.is_pending_insert(4), "the insert placeholder is recognized");
    check(!edits.is_pending_insert(2), "a real uid is not a placeholder");
    check(edits.drop_insert(4), "dropping the pending insert works");
    check(!edits.is_pending_insert(4), "the insert is gone");
    check(!edits.drop_insert(4), "dropping it twice is refused");
}

void test_without_structural_edits() {
    app::PlanInput plan = make_plan();
    // A cell edit the user already made: task 1 has a new Start.
    const date32 edited_start = from_ymd(2026, 2, 2, 8, 0);
    plan.tasks[0].start = edited_start;
    plan.tasks[0].name = "Renamed";

    app::StructuralEdits edits;
    edits.push(insert_after("New Task", 4, 2));
    edits.push(delete_uid(3));
    const app::PlanInput shown = edits.apply(plan);
    const app::PlanInput recovered = edits.without_structural_edits(shown, plan);

    // Placeholders are gone and the deleted task is back, in file order.
    check_size(recovered.tasks.size(), 3, "the recovered plan has the file's task count");
    check(recovered.tasks[0].uid == 1 && recovered.tasks[1].uid == 2 &&
              recovered.tasks[2].uid == 3,
          "the recovered plan is in file order");
    check(recovered.tasks[0].start == edited_start, "the cell edit survives the recovery");
    check_text(recovered.tasks[0].name, "Renamed", "the renamed value survives");
    check_size(recovered.links.size(), 2, "the links the delete would drop are restored");
    check_size(recovered.assignments.size(), 1, "the assignment is restored");

    // Re-applying the edits on the recovered plan is the display plan again, and
    // the cell edit is still there.
    const app::PlanInput rebuilt = edits.apply(recovered);
    check_size(rebuilt.tasks.size(), 3, "the rebuilt display plan has the same shape");
    check(rebuilt.tasks[0].start == edited_start, "the rebuilt plan keeps the cell edit");
    check_size(rebuilt.links.size(), 1, "only the deleted task's link is dropped");
}

void test_apply_placement() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    check(edits.apply(plan).tasks.size() == 3, "an empty edit list changes nothing");
    check(edits.next_placeholder_uid(plan) == 4, "the next placeholder uid is one past the max");

    edits.push(insert_after("Middle", 4, 1));
    app::PlanInput shown = edits.apply(plan);
    check_size(shown.tasks.size(), 4, "the insert is in the display plan");
    check(shown.tasks[0].uid == 1 && shown.tasks[1].uid == 4 && shown.tasks[2].uid == 2,
          "the insert is displayed after its anchor");
    check_text(shown.tasks[1].name, "Middle", "the placeholder carries the name");
    check(shown.tasks[1].start == plan.tasks.back().start,
          "the placeholder copies the last live task's start (the writer's add shape)");
    check(shown.tasks[1].duration_ticks == plan.tasks.back().duration_ticks,
          "the placeholder copies the last live task's duration");
    check(shown.tasks[1].id == 0, "the placeholder has no file id yet");
    check(edits.next_placeholder_uid(plan) == 5, "the next placeholder uid accounts for it");

    app::StructuralEdits at_end;
    at_end.push(insert_after("End", 9, 0));
    shown = at_end.apply(plan);
    check_size(shown.tasks.size(), 4, "an unanchored insert is in the plan");
    check(shown.tasks.back().uid == 9, "anchor 0 puts the row at the end");

    // A delete removes the task and exactly the rows the writer's cascade
    // tombstones: its links (either endpoint) and its assignments.
    app::StructuralEdits deletion;
    deletion.push(delete_uid(2));
    shown = deletion.apply(plan);
    check_size(shown.tasks.size(), 2, "the deleted task is gone");
    check_size(shown.links.size(), 0, "both links naming the task are gone");
    check_size(shown.assignments.size(), 0, "its assignment is gone");
    check_size(shown.skipped_links, plan.skipped_links + 2,
               "the two dropped links are counted as skipped");
    check_size(app::count_delete_dependants(plan, 2), 3,
               "a delete of uid 2 would tombstone 2 links and 1 assignment");
    check_size(app::count_delete_dependants(plan, 1), 1, "uid 1 has one link");
    check_size(app::count_delete_dependants(plan, 99), 0, "an unknown uid has none");

    // An insert anchored to a task that the same edit set deletes lands at the
    // end rather than disappearing.
    app::StructuralEdits mixed;
    mixed.push(delete_uid(2));
    mixed.push(insert_after("Orphan", 7, 2));
    shown = mixed.apply(plan);
    check_size(shown.tasks.size(), 3, "the delete and the insert both apply");
    check(shown.tasks.back().uid == 7, "an insert with a dead anchor lands at the end");
    check(shown.tasks[0].uid == 1 && shown.tasks[1].uid == 3, "the surviving order is kept");
}

// ---------------------------------------------------------------------------
// The real writer sequence
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)

struct Loaded {
    app::PlanInput plan;
    std::string path;
    bool ok = false;
};

Loaded load(const std::filesystem::path& path) {
    Loaded loaded;
    loaded.path = path.string();
    try {
        const mpp::Document document = mpp::load_file(loaded.path);
        loaded.plan = app::plan_input_from_document(document);
        loaded.ok = true;
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: cannot load %s: %s\n", loaded.path.c_str(), error.what());
    }
    return loaded;
}

// The uid whose delete would tombstone the most dependent rows (the middle of the
// fixture's FS chain); 0 when the fixture has no links.
std::uint32_t busiest_uid(const app::PlanInput& plan, std::size_t& dependants) {
    std::uint32_t best = 0;
    dependants = 0;
    for (const app::TaskRow& row : plan.tasks) {
        const std::size_t count = app::count_delete_dependants(plan, row.uid);
        if (count > dependants) {
            dependants = count;
            best = row.uid;
        }
    }
    return best;
}

void test_real_save_round_trip() {
    const std::filesystem::path fixture =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
    if (!std::filesystem::exists(fixture)) {
        std::printf("SKIP: %s is not present; the edit round trip needs the committed fixture\n",
                    fixture.string().c_str());
        return;
    }
    const std::size_t fixture_bytes = bytes_on_disk(fixture);
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);

    // Work on a COPY under the build tree: the committed fixture is read-only for
    // this test (and the copy proves the "source is never written" contract below).
    const std::filesystem::path copy =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-source.mpp";
    std::filesystem::remove(copy, error);
    std::filesystem::copy_file(fixture, copy, std::filesystem::copy_options::overwrite_existing,
                               error);
    check(!error, "the fixture was copied into the build tree");
    const std::size_t copy_bytes = bytes_on_disk(copy);

    Loaded base = load(copy);
    if (!base.ok || base.plan.tasks.empty()) {
        return;
    }
    check_size(base.plan.tasks.size(), 3, "the fixture has three tasks");
    // The committed fixture's dependent rows are assignments; the writer's delete
    // cascade covers both kinds (the pure test above pins the link half).
    check_size(base.plan.links.size(), 0, "the fixture has no link rows");
    check_size(base.plan.assignments.size(), 3, "the fixture has one assignment per task");
    check(base.plan.assignments.front().task_uid == base.plan.tasks.front().uid,
          "the first assignment names the first task");

    // ---- insert -----------------------------------------------------------
    const std::uint32_t anchor = base.plan.tasks[1].uid;
    app::StructuralEdits inserts;
    check(inserts.push(insert_after("New Task", inserts.next_placeholder_uid(base.plan),
                                    anchor)),
          "the insert is recorded");
    const app::PlanInput shown = inserts.apply(base.plan);
    check_size(shown.tasks.size(), 4, "the display plan shows the new row");

    const std::filesystem::path added =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-added.mpp";
    std::filesystem::remove(added, error);
    const app::StructuralSaveResult add_outcome =
        app::save_structural_edits(inserts, {}, copy.string(), added.string(), false);
    check(add_outcome.ok, "the insert saves");
    if (!add_outcome.ok) {
        std::printf("SAVE ERROR: %s\n", add_outcome.error.c_str());
        return;
    }
    check(add_outcome.error.empty(), "a successful structural save has no error");
    check_size(add_outcome.added, 1, "one row was added");
    check_size(add_outcome.deleted, 0, "nothing was deleted");
    check_size(add_outcome.added_uids.size(), 1, "the writer reported the new uid");
    check(!add_outcome.api.empty(), "the outcome names the writer calls");
    std::printf("EDIT: %s\n", add_outcome.api.c_str());
    for (const std::string& note : add_outcome.notes) {
        std::printf("EDIT NOTE: %s\n", note.c_str());
    }
    for (const std::string& warning : add_outcome.warnings) {
        std::printf("EDIT WARNING: %s\n", warning.c_str());
    }
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched by the insert save");
    check(std::filesystem::exists(added), "the inserted file exists");
    check(!std::filesystem::exists(added.string() + ".w51-structural-tmp"),
          "no intermediate file is left behind");

    Loaded after_add = load(added);
    if (!after_add.ok) {
        return;
    }
    check_size(after_add.plan.tasks.size(), 4, "the saved file has four tasks");
    const std::uint32_t new_uid =
        add_outcome.added_uids.empty() ? 0u : add_outcome.added_uids.front();
    check(new_uid != 0, "the writer allocated a non-zero uid");
    check(new_uid != anchor, "the new uid is not the anchor's");
    bool found = false;
    bool found_last = false;
    for (std::size_t index = 0; index < after_add.plan.tasks.size(); ++index) {
        if (after_add.plan.tasks[index].uid != new_uid) {
            continue;
        }
        found = true;
        found_last = index + 1 == after_add.plan.tasks.size();
        check_text(after_add.plan.tasks[index].name, "New Task", "the new row has its name");
    }
    check(found, "the new task reads back from the file");
    check(found_last, "the writer appends, so the new row is last in the file");

    // ---- delete, with the cascade -----------------------------------------
    std::size_t dependants = 0;
    const std::uint32_t doomed = busiest_uid(after_add.plan, dependants);
    check(dependants >= 1, "the fixture has a dependent row to cascade");
    std::size_t dependant_links = 0;
    std::size_t dependant_assignments = 0;
    for (const app::LinkRow& link : after_add.plan.links) {
        if (link.predecessor_uid == doomed || link.successor_uid == doomed) {
            ++dependant_links;
        }
    }
    for (const app::AssignmentRow& assignment : after_add.plan.assignments) {
        if (assignment.task_uid == doomed) {
            ++dependant_assignments;
        }
    }
    check_size(dependant_links + dependant_assignments, dependants,
               "the dependant count is links plus assignments");
    app::StructuralEdits deletes;
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_task;
    remove.uid = doomed;
    remove.dependants = dependants;
    check(deletes.push(remove), "the delete is recorded");
    const app::PlanInput delete_shown = deletes.apply(after_add.plan);
    check_size(delete_shown.tasks.size(), after_add.plan.tasks.size() - 1,
               "the display plan hides the deleted task");
    check_size(delete_shown.links.size(), 0, "the display plan hides its links");

    const std::filesystem::path deleted =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-deleted.mpp";
    std::filesystem::remove(deleted, error);
    const app::StructuralSaveResult delete_outcome =
        app::save_structural_edits(deletes, {}, added.string(), deleted.string(), false);
    check(delete_outcome.ok, "the delete saves");
    if (!delete_outcome.ok) {
        std::printf("SAVE ERROR: %s\n", delete_outcome.error.c_str());
        return;
    }
    check_size(delete_outcome.deleted, 1, "one row was tombstoned");
    check_size(delete_outcome.added, 0, "nothing was added");
    check_size(delete_outcome.links_tombstoned, dependant_links,
               "every dependent link was tombstoned");
    check_size(delete_outcome.assignments_tombstoned, dependant_assignments,
               "every dependent assignment was tombstoned");
    const std::size_t added_bytes = bytes_on_disk(added);
    check(added_bytes != 0, "the file that was deleted from exists");
    check_size(bytes_on_disk(deleted) != 0 ? 1u : 0u, 1u, "the delete wrote a file");

    Loaded after_delete = load(deleted);
    if (!after_delete.ok) {
        return;
    }
    check_size(after_delete.plan.tasks.size(), 3, "the saved file has three tasks again");
    check_size(after_delete.plan.links.size(),
               after_add.plan.links.size() - dependant_links,
               "the cascade removed the dependent links");
    check_size(after_delete.plan.assignments.size(),
               after_add.plan.assignments.size() - dependant_assignments,
               "the cascade removed the dependent assignments");
    for (const app::TaskRow& row : after_delete.plan.tasks) {
        check(row.uid != doomed, "the deleted uid is not a live task");
    }

    // ---- structural + start-date edits in one save ------------------------
    Loaded combined_base = load(copy);
    if (combined_base.ok && !combined_base.plan.tasks.empty()) {
        app::StructuralEdits combined;
        check(combined.push(insert_after("Combined", combined.next_placeholder_uid(combined_base.plan),
                                         0)),
              "the combined insert is recorded");
        const date32 new_start = from_ymd(2026, 3, 2, 8, 0);
        std::vector<app::StartEdit> starts;
        app::StartEdit start_edit;
        start_edit.uid = combined_base.plan.tasks.front().uid;
        start_edit.start = new_start;
        starts.push_back(start_edit);
        const std::filesystem::path both =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-combined.mpp";
        std::filesystem::remove(both, error);
        const app::StructuralSaveResult outcome = app::save_structural_edits(
            combined, starts, copy.string(), both.string(), false);
        check(outcome.ok, "a structural and a start edit save together");
        if (outcome.ok) {
            check_size(outcome.added, 1, "the insert landed");
            check_size(outcome.start_rows_written, 1, "the start edit landed");
            check(outcome.api.find("save_date_edits") != std::string::npos,
                  "the normal start-date path ran too");
            check(!std::filesystem::exists(both.string() + ".w51-structural-tmp"),
                  "the intermediate file is removed");
            Loaded reread = load(both);
            if (reread.ok) {
                check_size(reread.plan.tasks.size(), 4, "the combined file has four tasks");
                for (const mpp::Task& task : mpp::load_file(both.string()).tasks) {
                    if (task.uid != start_edit.uid) {
                        continue;
                    }
                    check_size(task.constraint_type, 4, "the edited start is a SNET pin");
                    check(task.constraint_date.raw == app::packed_from_date32(new_start),
                          "the constraint date is the edited start");
                }
            }
        }
    }

    // ---- refusals ---------------------------------------------------------
    app::StructuralEdits empty_edits;
    const app::StructuralSaveResult none =
        app::save_structural_edits(empty_edits, {}, copy.string(), added.string(), false);
    check(!none.ok, "an empty edit list is refused");
    check(!none.error.empty(), "the empty-edit refusal explains itself");

    app::StructuralEdits one;
    check(one.push(insert_after("No Target", one.next_placeholder_uid(base.plan), 0)),
          "the refusal fixture is recorded");
    const app::StructuralSaveResult no_target =
        app::save_structural_edits(one, {}, copy.string(), std::string(), false);
    check(!no_target.ok, "an empty target is refused");
    check(no_target.error.find("target") != std::string::npos,
          "the no-target refusal names the target");

    const std::filesystem::path untouched =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-refused.mpp";
    std::filesystem::remove(untouched, error);
    const app::StructuralSaveResult same_file =
        app::save_structural_edits(one, {}, copy.string(), copy.string(), false);
    check(!same_file.ok, "writing over the opened file is refused");
    check(same_file.error.find("refusing") != std::string::npos,
          "the overwrite refusal explains itself");
    check_size(bytes_on_disk(copy), copy_bytes, "the refused save left the source untouched");

    // The app's own file may be updated in place (the shell reloads it after a
    // structural save, so the next Save must be able to write it).
    const std::filesystem::path own =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-own.mpp";
    std::filesystem::remove(own, error);
    std::filesystem::copy_file(fixture, own, std::filesystem::copy_options::overwrite_existing,
                               error);
    app::StructuralEdits own_edits;
    Loaded own_loaded = load(own);
    if (own_loaded.ok) {
        check(own_edits.push(insert_after("In Place", own_edits.next_placeholder_uid(own_loaded.plan),
                                          0)),
              "the in-place insert is recorded");
        const app::StructuralSaveResult in_place = app::save_structural_edits(
            own_edits, {}, own.string(), own.string(), true);
        check(in_place.ok, "allow_source_overwrite lets the app update its own file");
        if (in_place.ok) {
            Loaded reread = load(own);
            check(reread.ok && reread.plan.tasks.size() == 4,
                  "the in-place file has the new task");
        }
    }

    // A delete of a uid that is not in the file fails cleanly.
    app::StructuralEdits bogus;
    check(bogus.push(delete_uid(999999u)), "the bogus delete is recorded");
    const std::filesystem::path bogus_target =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-bogus.mpp";
    std::filesystem::remove(bogus_target, error);
    const app::StructuralSaveResult bogus_outcome = app::save_structural_edits(
        bogus, {}, copy.string(), bogus_target.string(), false);
    check(!bogus_outcome.ok, "deleting an unknown uid is refused");
    check(!bogus_outcome.error.empty(), "the unknown-uid refusal explains itself");
    check(!std::filesystem::exists(bogus_target), "a failed save leaves no file behind");

    check_size(bytes_on_disk(fixture), fixture_bytes,
               "the committed fixture is never written");
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched by every save");
}

#else
void test_real_save_round_trip() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
}
#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_push_and_undo();
    test_without_structural_edits();
    test_apply_placement();
    test_real_save_round_trip();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("app_edit_tests: %d checks passed\n", g_checks);
    return 0;
}
