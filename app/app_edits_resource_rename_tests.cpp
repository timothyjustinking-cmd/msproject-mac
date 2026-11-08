// app/app_edits_resource_rename_tests.cpp -- Resource Information's Resource
// name through the app's edit model and save cascade against Project's own
// save pair (wave 34, W34-4.1).
//
// The committed pair is tests/data/wave30-resource-rename-control.mpp ->
// wave30-resource-rename.mpp: Project 2021 opened the control, renamed resource
// UniqueID 2 to a longer name and saved.  wave30-resource-rename-control-null.mpp
// is Project's own no-edit save of the same control (the null-save mask source).
//
// This test:
//
//   1. loads the control through the app's reader adapter, takes Project's
//      saved name from its own save and builds the edit with
//      make_rename_resource_edit();
//   2. queues it, proves the DISPLAY changes exactly that resource's name
//      (every other resource field, the tasks and the assignments stay), and
//      proves undo/redo -- including undo from the DISPLAYED plan, which the
//      shells use;
//   3. saves it with the real cascade and compares every stream of the port's
//      save against Project's under the null-save discipline (save_pair::
//      compare_all), with the four owned streams (114/Props, TBkndRsc
//      VarMeta and Var2Data, Props14) DECLARED changed so a mask that swallowed
//      the change fails `effects_ok`; the pair's 16 effect bytes are counted;
//   4. reopens the port's save through the independent core/mpp reader and
//      proves the saved name is the name Project saved.
//
// It also checks every refusal the edit model owes the user: the project row
// (uid 0), an unknown resource, an empty / blank / control-character /
// non-ASCII name, an unchanged name, a same-size and a shorter name (the
// unmeasured shrink re-point), a resource an assignment names (the pair
// renamed an unassigned one), the edit's exclusivity (in the queue and in the
// cascade, with another structural edit, with a task-mode edit and with a
// start-date edit) and a save-time refusal on a file that changed after the
// edit was queued.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w34-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w34-app --config Release --target app_edits_resource_rename_tests
//   ctest --test-dir build\w34-app -C Release -R "^app_edits_resource_rename$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "fixture_guard.h"
#include "layout_model.h"
// core/mppwrite/outline.h includes core/mppwrite/table.h from its own
// directory.  The app include path lists core/view first, so a bare "table.h"
// here would find core/view/table.h instead; outline.h pins the mppwrite header
// that find_var_offsets / encode_utf16z / check_table live in (the same rule as
// app_edits_task_mode_tests.cpp).  varmeta_edit.h is the writer under test.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "save_pair_compare.h"
#include "save_pair_noise.h"
#include "varmeta_edit.h"

#if !defined(APP_TEST_DATA_DIR) || !defined(APP_TEST_OUT_DIR)
int main() {
    std::printf("FAIL: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined; "
                "configure with APP_BUILD_TESTS=ON (app/CMakeLists.txt)\n");
    return 1;
}
#else

namespace {

using Bytes = std::vector<std::uint8_t>;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

void check_size(std::size_t got, std::size_t want, const std::string& what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what.c_str(),
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

/* The Context save_pair::compare_all reports through. */
struct Context {
    void check(bool ok, const std::string& what) const { ::check(ok, what); }
    void expected_failure(bool unexpected_pass, const std::string& what) const {
        /* This pair has no KnownGap entry: any expected-failure report is a
         * failure of the test itself. */
        ::check(false, "unexpected KnownGap report: " + what +
                           (unexpected_pass ? " (gap gone)" : ""));
    }
};

const char* const kDataName = "wave30-resource-rename-control.mpp";
const char* const kResultName = "wave30-resource-rename.mpp";

const std::string kMetaStream = "114/TBkndRsc/VarMeta";
const std::string kDataStream = "114/TBkndRsc/Var2Data";

std::filesystem::path data_path(const std::string& name) {
    return std::filesystem::path(APP_TEST_DATA_DIR) / name;
}

std::filesystem::path out_path(const std::string& name) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    return std::filesystem::path(APP_TEST_OUT_DIR) / name;
}

const app::ResourceRow* find_resource(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::ResourceRow& row : plan.resources) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

bool same_resource_but_name(const app::ResourceRow& left, const app::ResourceRow& right) {
    return left.uid == right.uid && left.id == right.id && left.initials == right.initials &&
           left.type == right.type && left.max_units_percent == right.max_units_percent &&
           left.group == right.group && left.code == right.code &&
           left.material_label == right.material_label &&
           left.base_calendar == right.base_calendar &&
           left.standard_rate_per_hour == right.standard_rate_per_hour &&
           left.overtime_rate_per_hour == right.overtime_rate_per_hour &&
           left.cost_per_use == right.cost_per_use && left.accrue_at == right.accrue_at;
}

bool same_assignments(const app::PlanInput& left, const app::PlanInput& right) {
    if (left.assignments.size() != right.assignments.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.assignments.size(); ++index) {
        const app::AssignmentRow& a = left.assignments[index];
        const app::AssignmentRow& b = right.assignments[index];
        if (a.uid != b.uid || a.task_uid != b.task_uid || a.resource_uid != b.resource_uid ||
            a.work_ticks != b.work_ticks || a.units_percent != b.units_percent) {
            return false;
        }
    }
    return true;
}

Bytes stream_or_empty(const mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

/* --- the measured pair ---------------------------------------------------- */

void check_pair() {
    const std::filesystem::path control_path = data_path(kDataName);
    const std::filesystem::path result_path = data_path(kResultName);
    const std::filesystem::path null_path =
        data_path(save_pair::null_fixture_name(kDataName));
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);

    app::PlanInput plan;
    app::PlanInput project_plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
        project_plan = app::plan_input_from_document(mpp::load_file(result_path.string()));
    } catch (const std::exception& error) {
        check(false, "the rename control and Project's save load through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::ResourceRow* control_row = find_resource(plan, 2);
    const app::ResourceRow* project_row = find_resource(project_plan, 2);
    check(control_row != nullptr, "the control has resource uid 2");
    check(project_row != nullptr, "Project's save has resource uid 2");
    if (control_row == nullptr || project_row == nullptr) {
        return;
    }
    const std::string new_name = project_row->name;
    check(new_name != control_row->name, "Project's save renamed resource uid 2");
    check_size(new_name.size(), 3, "Project's saved name is three characters");
    check_size(control_row->name.size(), 2, "the control's name is two characters");
    /* The pair's resource has no assignment row: that is the whole measured
     * shape, and why a rename of an assigned resource is refused below. */
    check_size(static_cast<std::size_t>(std::count_if(
                   plan.assignments.begin(), plan.assignments.end(),
                   [](const app::AssignmentRow& row) { return row.resource_uid == 2; })),
               0, "the pair's resource uid 2 has no assignment row");

    /* --- make_rename_resource_edit --------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_rename_resource_edit(plan, control_path.string(), 2,
                                                      new_name, edit, edit_error);
    check(built, "make_rename_resource_edit accepts the measured shape");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::rename_resource, "the built edit has the rename kind");
    check_size(edit.uid, 2, "the edit names resource uid 2");
    check(edit.name == new_name, "the edit carries the new name");

    /* The name is trimmed (ASCII whitespace) before it is queued. */
    {
        app::StructuralEdit padded;
        std::string padded_error;
        check(app::make_rename_resource_edit(plan, control_path.string(), 2,
                                             "  " + new_name + " \t", padded, padded_error) &&
                  padded.name == new_name,
              "a name is trimmed before it is queued");
    }

    /* --- push + display + undo/redo -------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the rename edit is queued");
    const app::PlanInput shown = edits.apply(plan);
    const app::ResourceRow* shown_row = find_resource(shown, 2);
    check(shown_row != nullptr && shown_row->name == new_name,
          "the display shows the new name");
    check(shown_row != nullptr && same_resource_but_name(*shown_row, *control_row),
          "the display changes only the name");
    check_size(shown.resources.size(), plan.resources.size(),
               "the display keeps every resource row");
    for (const app::ResourceRow& row : plan.resources) {
        if (row.uid == 2) {
            continue;
        }
        const app::ResourceRow* other = find_resource(shown, row.uid);
        check(other != nullptr && other->name == row.name && same_resource_but_name(*other, row),
              "the display leaves every other resource alone");
    }
    check(same_assignments(shown, plan), "the display leaves the assignments alone");
    check_size(shown.tasks.size(), plan.tasks.size(), "the display keeps the task count");

    check(edits.undo(), "the rename undoes");
    const app::PlanInput undone = edits.apply(plan);
    check(find_resource(undone, 2) != nullptr && find_resource(undone, 2)->name == control_row->name,
          "undo restores the old name");
    const app::PlanInput undone_from_display =
        edits.apply(edits.without_structural_edits(shown, plan));
    check(find_resource(undone_from_display, 2) != nullptr &&
              find_resource(undone_from_display, 2)->name == control_row->name,
          "undo from the displayed plan restores the old name");
    check(edits.redo(), "the rename redoes");
    const app::PlanInput redone = edits.apply(plan);
    check(find_resource(redone, 2) != nullptr && find_resource(redone, 2)->name == new_name,
          "redo shows the new name again");
    const app::PlanInput redone_from_display =
        edits.apply(edits.without_structural_edits(shown, plan));
    check(find_resource(redone_from_display, 2) != nullptr &&
              find_resource(redone_from_display, 2)->name == new_name,
          "redo from the displayed plan keeps the new name");

    /* --- save through the real cascade ----------------------------------- */
    const std::filesystem::path out = out_path("app-resource-rename.mpp");
    std::error_code error;
    std::filesystem::remove(out, error);
    const app::StructuralSaveResult saved =
        app::save_structural_edits(edits, {}, control_path.string(), out.string(), false);
    check(saved.ok, "the rename save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.resources_renamed, 1, "the cascade wrote the rename");
    check(saved.api.find("rename_resource") != std::string::npos,
          "the save's api names rename_resource");
    check(saved.warnings.empty(), "the rename save has no warnings");
    check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
          "the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument null_save;
    mppwrite::CfbDocument project;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        null_save = mppwrite::CfbDocument::from_file(null_path.string());
        project = mppwrite::CfbDocument::from_file(result_path.string());
        output = mppwrite::CfbDocument::from_file(out.string());
    } catch (const std::exception& open_error) {
        check(false, "the control, the null save, the Project pair and the port's save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* Every stream, under the null-save discipline.  The four owned streams
     * are declared changed by this pair, so `effects_ok` must hold for each. */
    const std::array<const char*, 4> declared{
        {"114/Props", "114/TBkndRsc/Var2Data", "114/TBkndRsc/VarMeta", "Props14"}};
    const Context context;
    save_pair::compare_all(context, "resource-rename", control, null_save, project, output,
                           nullptr, 0, declared.data(), declared.size());

    /* The pair's effect bytes, counted: Props 1 + Var2Data 12 + VarMeta 2 +
     * Props14 1 = 16 (spec/writers/evidence/wave30-resource-rename.md). */
    std::size_t effects = 0;
    for (const char* path : declared) {
        const save_pair::Kind kind = save_pair::kind_for_path(path);
        const save_pair::Verdict verdict = save_pair::compare(
            kind, stream_or_empty(control, path), stream_or_empty(null_save, path),
            stream_or_empty(project, path), stream_or_empty(output, path), true);
        effects += verdict.effects + verdict.tail_effects;
        check(verdict.effects_ok, std::string(path) + ": the declared effect set is non-empty");
        check_size(verdict.mismatches, 0, std::string(path) + ": equals Project's save");
    }
    check_size(effects, 16, "the pair's effect bytes are the measured 16");

    /* Independent read-back from the file the cascade wrote: one name entry
     * for uid 2, at the offset Project stored, holding the encoding of the name
     * Project saved. */
    const std::vector<std::uint32_t> offsets = mppwrite::find_var_offsets(
        output.read_stream(kMetaStream), mppwrite::kResourceNameFieldId, 2);
    const std::vector<std::uint32_t> project_offsets = mppwrite::find_var_offsets(
        project.read_stream(kMetaStream), mppwrite::kResourceNameFieldId, 2);
    check_size(offsets.size(), 1, "one name entry for resource uid 2");
    check(offsets == project_offsets, "the name entry is at the offset Project stored");
    if (offsets.size() == 1) {
        const Bytes block = mppwrite::read_var_block(output.read_stream(kDataStream), offsets.front());
        check(block == mppwrite::encode_utf16z(new_name),
              "the block on disk is the encoding of the name Project saved");
    }
    check(mppwrite::check_table(output).empty(), "the saved task table passes check_table");

    /* The reader and the display agree with Project's save. */
    try {
        const app::PlanInput reread =
            app::plan_input_from_document(mpp::load_file(out.string()));
        const app::ResourceRow* reread_row = find_resource(reread, 2);
        check(reread_row != nullptr && reread_row->name == new_name,
              "the reader reads back the new name from the port's save");
        check(reread_row != nullptr && same_resource_but_name(*reread_row, *control_row),
              "the reader reads back every other resource field unchanged");
        check_size(reread.resources.size(), plan.resources.size(),
                   "the save keeps the resource count");
        check(same_assignments(reread, plan), "the save keeps every assignment");
        check_size(reread.tasks.size(), plan.tasks.size(), "the save keeps the task count");
        for (const app::ResourceRow& row : plan.resources) {
            if (row.uid == 2) {
                continue;
            }
            const app::ResourceRow* other = find_resource(reread, row.uid);
            check(other != nullptr && other->name == row.name,
                  "the save keeps every other resource's name");
        }
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* --- refusals ------------------------------------------------------------- */

void expect_refused(const app::PlanInput& plan, const std::string& source, std::uint32_t uid,
                    const std::string& name, const char* needle, const std::string& what) {
    app::StructuralEdit edit;
    std::string error;
    const bool built = app::make_rename_resource_edit(plan, source, uid, name, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what.c_str());
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
    check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0 && edit.name.empty(),
          what + ": the refused edit is left default-constructed");
}

void check_refusals() {
    const std::filesystem::path control_path = data_path(kDataName);
    fixture::require_committed(control_path);
    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception&) {
        check(false, "the refusal fixture loads");
        return;
    }
    const std::string source = control_path.string();
    const app::ResourceRow* row = find_resource(plan, 2);
    if (row == nullptr) {
        check(false, "the refusal fixture has resource uid 2");
        return;
    }
    const std::string old_name = row->name;

    expect_refused(plan, source, 0, "Longer name", "not a saved row", "the project row (uid 0)");
    expect_refused(plan, source, 999, "Longer name", "not a saved row", "an unknown resource");
    expect_refused(plan, source, 2, "", "empty", "an empty name");
    expect_refused(plan, source, 2, " \t ", "empty", "a blank name");
    expect_refused(plan, source, 2, std::string("ab\0c", 4), "control character",
                   "an embedded NUL");
    expect_refused(plan, source, 2, "line\none", "control character", "a newline in the name");
    expect_refused(plan, source, 2, "Caf\xC3\xA9 Crew", "a name with non-ASCII characters",
                   "a non-ASCII name");
    expect_refused(plan, source, 2, old_name, "unchanged", "the unchanged name");
    expect_refused(plan, source, 2, "a", "a name that is not longer than the current one",
                   "a shorter name");
    {
        /* Same stored size, different text: the control's name is two
         * characters, so any other two-character name encodes to the same 6 B. */
        const std::string same_size = old_name == "zz" ? "yy" : "zz";
        expect_refused(plan, source, 2, same_size,
                       "a name that is not longer than the current one", "a same-size name");
    }

    /* A resource an assignment names: the pair renamed an unassigned resource,
     * so the assigned shape is refused by the builder and by push(). */
    {
        app::PlanInput assigned = plan;
        app::AssignmentRow assignment;
        assignment.uid = 900;
        assignment.task_uid = 1;
        assignment.resource_uid = 2;
        assignment.resource_uid_raw = 2;
        assigned.assignments.push_back(assignment);
        expect_refused(assigned, source, 2, "Longer name", "has assignments",
                       "a resource that has an assignment");
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::rename_resource;
        hand.uid = 2;
        hand.name = "Longer name";
        app::StructuralEdits edits;
        check(!edits.push(std::move(hand), &assigned),
              "push() refuses the rename of an assigned resource");
    }

    /* A file that changed after the edit was queued: Project's own saved file
     * already holds the longer name, so the same edit is no longer larger. */
    const std::filesystem::path result_path = data_path(kResultName);
    fixture::require_committed(result_path);
    {
        const app::PlanInput project_plan =
            app::plan_input_from_document(mpp::load_file(result_path.string()));
        const app::ResourceRow* project_row = find_resource(project_plan, 2);
        check(project_row != nullptr, "Project's save has resource uid 2 for the stale check");
        if (project_row != nullptr) {
            const std::string longer = project_row->name;
            expect_refused(project_plan, result_path.string(), 2, longer, "unchanged",
                           "the name Project already saved");
            app::StructuralEdit hand;
            hand.kind = app::StructuralKind::rename_resource;
            hand.uid = 2;
            hand.name = longer;
            app::StructuralEdits edits;
            check(edits.push(std::move(hand), &plan),
                  "a hand-built rename queues against the control's plan");
            const std::filesystem::path out = out_path("app-resource-rename-stale.mpp");
            std::error_code error;
            std::filesystem::remove(out, error);
            const app::StructuralSaveResult saved = app::save_structural_edits(
                edits, {}, result_path.string(), out.string(), false);
            check(!saved.ok, "the save fails on a file whose name is already longer");
            check(saved.error.find("not larger") != std::string::npos,
                  "the failed save names the strictly-larger rule (got \"" + saved.error + "\")");
            check(!std::filesystem::exists(out), "the failed save writes no target file");
            check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
                  "the failed save leaves no intermediate file");
        }
    }

    /* push(): the plan-side backstop for a hand-built queue. */
    auto hand_edit = [](std::uint32_t uid, const std::string& name) {
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::rename_resource;
        hand.uid = uid;
        hand.name = name;
        return hand;
    };
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(2, "Longer name"), nullptr),
              "push() refuses a rename with no plan");
        check(!edits.push(hand_edit(0, "Longer name"), &plan), "push() refuses uid 0");
        check(!edits.push(hand_edit(999, "Longer name"), &plan),
              "push() refuses an unknown resource");
        check(!edits.push(hand_edit(2, "  "), &plan), "push() refuses a blank name");
        check(!edits.push(hand_edit(2, std::string("a\0b", 3)), &plan),
              "push() refuses an embedded NUL");
        check(!edits.push(hand_edit(2, "Caf\xC3\xA9"), &plan), "push() refuses a non-ASCII name");
        check(edits.empty(), "no refused rename left anything in the queue");
    }

    /* Exclusivity, in both directions and with the other exclusive kind. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(2, "Longer name"), &plan),
              "push() refuses a rename over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(2, "Longer name"), &plan), "a rename queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan),
              "push() refuses another edit over a rename");
        check(!edits.push(hand_edit(2, "Longer name again"), &plan),
              "push() refuses a second rename");
        app::StructuralEdit task_mode;
        task_mode.kind = app::StructuralKind::set_task_mode;
        task_mode.uid = 1;
        task_mode.task_manual = true;
        check(!edits.push(std::move(task_mode), &plan), "push() refuses a task-mode edit over a rename");
    }

    /* The cascade refuses a start-date edit beside a rename before it writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_rename_resource_edit(plan, source, 2, "Longer name", edit, error),
              "the exclusivity fixture builds its rename");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the rename");
        const std::filesystem::path out = out_path("app-resource-rename-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses a rename with a start-date edit");
        check(saved.error.find("start-date") != std::string::npos,
              "the save's refusal names the start-date edit");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }
}

}  // namespace

int main() {
    check_pair();
    check_refusals();
    std::printf("app_edits_resource_rename_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_resource_rename_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
