// app/app_edits_assignment_tests.cpp -- Assign Resources and its removal through
// the app's edit model and save cascade against Project's own save pairs
// (wave 34, W34-4.3).
//
// The committed pairs (spec/writers/table-growth.md, "W34-3.1"):
//
//   wave30-assign-add-control.mpp      -> wave30-assign-add.mpp
//       task 1 held only Project's unassigned placeholder; Project assigned
//       resource 2 to it (100 %, the task's own work)
//   wave30-assign-delete-control.mpp   -> wave30-assign-delete.mpp
//       the same assignment removed again
//
// each with a committed null save of its control (the null-save mask source).
//
// This test, for each pair:
//
//   1. loads the control through the app's reader adapter and builds the edit
//      with make_assignment_edit();
//   2. queues it and proves the DISPLAY: the task's assignment row equals,
//      field by field, the row the reader reads from Project's own saved file,
//      every other assignment, resource and task is untouched, the task's
//      physical row count follows, and undo/redo (including undo from the
//      DISPLAYED plan, which the shells use) restore and re-apply it;
//   3. saves it with the real cascade and compares every stream of the port's
//      save against Project's under the null-save discipline (save_pair::
//      compare_all, the identity dwords declared through TailNormalisation),
//      with the pair's owned streams DECLARED changed so a mask that swallowed
//      the change fails `effects_ok`; the pair's effect bytes are counted;
//   4. reopens the port's save through the independent core/mpp reader and
//      proves the assignment rows are the ones Project saved.
//
// It also checks every refusal the edit model owes the user: a summary task, a
// started task, a resource that is not a 100 % work resource, a task that
// already has an assignment (the unmeasured counts), a removal that is not the
// task's one assignment of that resource, an unknown task or resource, a plan
// that disagrees with the file, a file that is not the measured shape or cannot
// be read, push()'s plan-side backstop, the edit's exclusivity (in the queue
// and in the cascade) and a save-time refusal on a file that changed after the
// edit was queued.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w34-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w34-app --config Release --target app_edits_assignment_tests
//   ctest --test-dir build\w34-app -C Release -R "^app_edits_assignment$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "fixture_guard.h"
#include "layout_model.h"
// core/mppwrite/outline.h pins the core/mppwrite/table.h header (a bare
// "table.h" would find core/view/table.h; the app include path lists
// core/view first), the same rule as app_edits_resource_rename_tests.cpp.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "save_pair_compare.h"
#include "save_pair_noise.h"

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
        /* These pairs have no KnownGap entry: any expected-failure report is a
         * failure of the test itself. */
        ::check(false, "unexpected KnownGap report: " + what +
                           (unexpected_pass ? " (gap gone)" : ""));
    }
};

const char* const kAssnFixed = "114/TBkndAssn/FixedData";
const char* const kAssnMeta = "114/TBkndAssn/FixedMeta";
const char* const kAssnF2 = "114/TBkndAssn/Fixed2Data";
const char* const kAssnF2Meta = "114/TBkndAssn/Fixed2Meta";
const char* const kAssnVarMeta = "114/TBkndAssn/VarMeta";
const char* const kAssnVar2 = "114/TBkndAssn/Var2Data";
const char* const kTaskFixed = "114/TBkndTask/FixedData";
const char* const kTaskMeta = "114/TBkndTask/FixedMeta";
const char* const kTaskVar2 = "114/TBkndTask/Var2Data";
const char* const kRscFixed = "114/TBkndRsc/FixedData";
const char* const kRscMeta = "114/TBkndRsc/FixedMeta";
const char* const kProps = "114/Props";
/* The stored path carries the OLE control prefix; "\005" is an octal escape, so
 * the following "D" is not part of it. */
const char* const kSummary = "\005DocumentSummaryInformation";
const char* const kMarker = "Props14";

/* The declared-effect stream lists, exactly the measured effect sets. */
const std::array<const char*, 14> kAddDeclared{{kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta,
                                                kAssnVarMeta, kAssnVar2, kTaskFixed, kTaskMeta,
                                                kTaskVar2, kRscFixed, kRscMeta, kProps,
                                                kSummary, kMarker}};
const std::array<const char*, 13> kRemoveDeclared{{kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta,
                                                   kAssnVarMeta, kAssnVar2, kTaskFixed,
                                                   kTaskMeta, kTaskVar2, kRscFixed, kProps,
                                                   kSummary, kMarker}};

struct Pair {
    const char* control;
    const char* null_save;
    const char* project;
    const char* label;
    bool assign;
    std::uint32_t task_uid;
    std::uint32_t resource_uid;
    const char* const* declared;
    std::size_t declared_count;
    std::size_t effects;  // the declared streams' effect bytes
};

const Pair kAdd{"wave30-assign-add-control.mpp",
                "wave30-assign-add-control-null.mpp",
                "wave30-assign-add.mpp",
                "assign-add",
                true,
                1,
                2,
                kAddDeclared.data(),
                kAddDeclared.size(),
                1012};
const Pair kRemove{"wave30-assign-delete-control.mpp",
                   "wave30-assign-delete-control-null.mpp",
                   "wave30-assign-delete.mpp",
                   "assign-remove",
                   false,
                   1,
                   2,
                   kRemoveDeclared.data(),
                   kRemoveDeclared.size(),
                   905};

std::filesystem::path data_path(const std::string& name) {
    return std::filesystem::path(APP_TEST_DATA_DIR) / name;
}

std::filesystem::path out_path(const std::string& name) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    return std::filesystem::path(APP_TEST_OUT_DIR) / name;
}

const app::TaskRow* find_task(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

const app::AssignmentRow* find_assignment(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::AssignmentRow& row : plan.assignments) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

/* The task's assignment row of the wanted kind: the real row for `resource`,
 * or the unassigned placeholder (resource_uid 0). */
const app::AssignmentRow* find_task_assignment(const app::PlanInput& plan, std::uint32_t task,
                                               bool real) {
    const app::AssignmentRow* found = nullptr;
    for (const app::AssignmentRow& row : plan.assignments) {
        if (row.task_uid == task && (row.resource_uid != 0) == real) {
            found = &row;
        }
    }
    return found;
}

bool same_assignment(const app::AssignmentRow& a, const app::AssignmentRow& b) {
    return a.uid == b.uid && a.task_uid == b.task_uid && a.resource_uid == b.resource_uid &&
           a.resource_uid_raw == b.resource_uid_raw && a.work_ticks == b.work_ticks &&
           a.units_percent == b.units_percent && a.start == b.start && a.finish == b.finish;
}

bool same_assignments(const app::PlanInput& left, const app::PlanInput& right) {
    if (left.assignments.size() != right.assignments.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.assignments.size(); ++index) {
        if (!same_assignment(left.assignments[index], right.assignments[index])) {
            return false;
        }
    }
    return true;
}

bool same_resources(const app::PlanInput& left, const app::PlanInput& right) {
    if (left.resources.size() != right.resources.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.resources.size(); ++index) {
        const app::ResourceRow& a = left.resources[index];
        const app::ResourceRow& b = right.resources[index];
        if (a.uid != b.uid || a.id != b.id || a.name != b.name || a.type != b.type ||
            a.max_units_percent != b.max_units_percent) {
            return false;
        }
    }
    return true;
}

Bytes stream_or_empty(const mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

/* The three identity counter dwords of the appended Fixed2Data record, which
 * Project renumbers on each save (the same declaration as tests/roundtrip_
 * assignment_growth.cpp). */
std::vector<save_pair::TailNormalisation> tail_normalisation(
    const mppwrite::CfbDocument& null_save) {
    const std::size_t base = null_save.read_stream(kAssnF2).size();
    save_pair::TailNormalisation entry;
    entry.path = kAssnF2;
    entry.ranges = {{base + 0, 4}, {base + 16, 4}, {base + 32, 4}};
    return {entry};
}

/* The assignment row Project's save holds for the edit: the real row on an
 * assign, the appended placeholder on a removal (the row whose uid is new). */
const app::AssignmentRow* project_new_row(const app::PlanInput& control,
                                          const app::PlanInput& project, const Pair& pair) {
    const app::AssignmentRow* found = nullptr;
    for (const app::AssignmentRow& row : project.assignments) {
        if (row.task_uid == pair.task_uid && (row.resource_uid != 0) == pair.assign &&
            find_assignment(control, row.uid) == nullptr) {
            found = &row;
        }
    }
    return found;
}

/* --- the measured pairs ---------------------------------------------------- */

void check_pair(const Pair& pair) {
    const std::string label = pair.label;
    const std::filesystem::path control_path = data_path(pair.control);
    const std::filesystem::path result_path = data_path(pair.project);
    const std::filesystem::path null_path = data_path(pair.null_save);
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);
    check(null_path.filename().string() == save_pair::null_fixture_name(pair.control),
          label + ": the null save is the control's committed null fixture");

    app::PlanInput plan;
    app::PlanInput project_plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
        project_plan = app::plan_input_from_document(mpp::load_file(result_path.string()));
    } catch (const std::exception& error) {
        check(false, label + ": the control and Project's save load through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::AssignmentRow* project_row = project_new_row(plan, project_plan, pair);
    check(project_row != nullptr, label + ": Project's save has the new assignment row");
    const app::TaskRow* control_task = find_task(plan, pair.task_uid);
    const app::TaskRow* project_task = find_task(project_plan, pair.task_uid);
    check(control_task != nullptr && project_task != nullptr,
          label + ": the task is in the control and in Project's save");
    if (project_row == nullptr || control_task == nullptr || project_task == nullptr) {
        return;
    }
    check(!control_task->summary && control_task->percent_complete == 0 &&
              control_task->actual_start == app::kDateNa,
          label + ": the measured task is an unstarted leaf");

    /* --- make_assignment_edit --------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_assignment_edit(plan, control_path.string(), pair.task_uid,
                                                 pair.resource_uid, pair.assign, edit, edit_error);
    check(built, label + ": make_assignment_edit accepts the measured shape");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == (pair.assign ? app::StructuralKind::assign_resource
                                    : app::StructuralKind::unassign_resource),
          label + ": the built edit has the right kind");
    check_size(edit.uid, pair.task_uid, label + ": the edit names the task");
    check_size(edit.resource_uid, pair.resource_uid, label + ": the edit names the resource");
    check_size(edit.assignment_uid, project_row->uid,
               label + ": the edit allocates the assignment uid Project allocated");

    /* --- push + display + undo/redo --------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the edit is queued");
    const app::PlanInput shown = edits.apply(plan);
    check_size(shown.assignments.size(), plan.assignments.size(),
               label + ": the display keeps the live assignment count");
    const app::AssignmentRow* shown_row = find_assignment(shown, project_row->uid);
    check(shown_row != nullptr, label + ": the display shows the new assignment row");
    if (shown_row != nullptr) {
        /* Field by field against the row the reader reads from Project's own
         * saved file. */
        check_size(shown_row->task_uid, project_row->task_uid, label + ": display task equals Project's");
        check_size(shown_row->resource_uid, project_row->resource_uid,
                   label + ": display resource equals Project's");
        check_size(shown_row->resource_uid_raw, project_row->resource_uid_raw,
                   label + ": display raw resource word equals Project's");
        check(shown_row->work_ticks == project_row->work_ticks,
              label + ": display work equals Project's (" + std::to_string(shown_row->work_ticks) +
                  " vs " + std::to_string(project_row->work_ticks) + ")");
        check(shown_row->units_percent == project_row->units_percent,
              label + ": display units equal Project's");
        check(shown_row->start == project_row->start && shown_row->finish == project_row->finish,
              label + ": display start and finish equal Project's");
    }
    check(find_assignment(shown, pair.assign ? find_task_assignment(plan, pair.task_uid, false)->uid
                                             : find_task_assignment(plan, pair.task_uid, true)->uid) ==
              nullptr,
          label + ": the display replaces the row the writer tombstones");
    for (const app::AssignmentRow& row : plan.assignments) {
        if (row.task_uid == pair.task_uid) {
            continue;
        }
        const app::AssignmentRow* other = find_assignment(shown, row.uid);
        check(other != nullptr && same_assignment(*other, row),
              label + ": the display leaves every other assignment alone");
    }
    check(same_resources(shown, plan), label + ": the display leaves the resources alone");
    check_size(shown.tasks.size(), plan.tasks.size(), label + ": the display keeps the tasks");
    const app::TaskRow* shown_task = find_task(shown, pair.task_uid);
    check(shown_task != nullptr && shown_task->assignment_rows == project_task->assignment_rows,
          label + ": the task's physical assignment rows follow Project's (" +
              (shown_task != nullptr ? std::to_string(shown_task->assignment_rows) : "-") +
              " vs " + std::to_string(project_task->assignment_rows) + ")");

    check(edits.undo(), label + ": the edit undoes");
    check(same_assignments(edits.apply(plan), plan), label + ": undo restores the assignments");
    check(same_assignments(edits.apply(edits.without_structural_edits(shown, plan)), plan),
          label + ": undo from the displayed plan restores them");
    check(edits.redo(), label + ": the edit redoes");
    check(same_assignments(edits.apply(plan), shown), label + ": redo shows the same assignments");
    check(same_assignments(edits.apply(edits.without_structural_edits(shown, plan)), shown),
          label + ": redo from the displayed plan shows them once");

    /* --- save through the real cascade ------------------------------------ */
    const std::filesystem::path out = out_path(std::string("app-") + pair.label + ".mpp");
    std::error_code error;
    std::filesystem::remove(out, error);
    const app::StructuralSaveResult saved =
        app::save_structural_edits(edits, {}, control_path.string(), out.string(), false);
    check(saved.ok, label + ": the save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.assignments_written, 1, label + ": the cascade wrote the assignment");
    check(saved.api.find(pair.assign ? " assign_resource" : " unassign_resource") !=
              std::string::npos,
          label + ": the save's api names the writer");
    check(saved.warnings.empty(), label + ": the save has no warnings");
    check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
          label + ": the save leaves no intermediate file");

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
        check(false, label + ": the control, null save, Project pair and port save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* Every stream, under the null-save discipline. */
    const Context context;
    const std::vector<save_pair::TailNormalisation> normalisation = tail_normalisation(null_save);
    save_pair::compare_all(context, label, control, null_save, project, output, nullptr, 0,
                           pair.declared, pair.declared_count, normalisation);

    std::size_t effects = 0;
    for (std::size_t index = 0; index < pair.declared_count; ++index) {
        const char* path = pair.declared[index];
        const save_pair::Kind kind = save_pair::kind_for_path(path);
        const save_pair::Verdict verdict = save_pair::compare(
            kind, stream_or_empty(control, path), stream_or_empty(null_save, path),
            stream_or_empty(project, path), stream_or_empty(output, path), true,
            save_pair::normalisation_for(normalisation, path));
        effects += verdict.effects + verdict.tail_effects;
        check_size(verdict.mismatches, 0, label + ": " + path + ": equals Project's save");
    }
    std::printf("   %s: declared effect bytes = %llu\n", pair.label,
                static_cast<unsigned long long>(effects));
    check_size(effects, pair.effects, label + ": the pair's declared effect bytes");

    /* Every stream OUTSIDE the declared ones is byte-identical to the control. */
    for (const std::string& path : control.stream_paths()) {
        bool owned = false;
        for (std::size_t index = 0; index < pair.declared_count; ++index) {
            owned = owned || path == pair.declared[index];
        }
        if (owned) {
            continue;
        }
        check(stream_or_empty(output, path) == stream_or_empty(control, path),
              label + ": " + path + ": untouched by the edit");
    }
    check(mppwrite::check_table(output).empty(), label + ": the saved task table passes check_table");

    /* The independent reader reads back what Project saved. */
    try {
        const app::PlanInput reread = app::plan_input_from_document(mpp::load_file(out.string()));
        check(same_assignments(reread, project_plan),
              label + ": the reader reads back Project's assignment rows, field by field");
        check(same_resources(reread, plan), label + ": the save keeps every resource");
        check_size(reread.tasks.size(), plan.tasks.size(), label + ": the save keeps the tasks");
        const app::TaskRow* reread_task = find_task(reread, pair.task_uid);
        check(reread_task != nullptr && reread_task->assignment_rows == project_task->assignment_rows,
              label + ": the reread task's physical assignment rows equal Project's");
        /* The display, the save and Project agree. */
        check(same_assignments(shown, reread),
              label + ": the displayed assignments equal the rows read back from the save");
    } catch (const std::exception& read_error) {
        check(false, label + ": the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* --- refusals -------------------------------------------------------------- */

void expect_refused(const app::PlanInput& plan, const std::string& source, std::uint32_t task,
                    std::uint32_t resource, bool assign, const char* needle,
                    const std::string& what) {
    app::StructuralEdit edit;
    std::string error;
    const bool built = app::make_assignment_edit(plan, source, task, resource, assign, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what.c_str());
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
    check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0 &&
              edit.resource_uid == 0 && edit.assignment_uid == 0,
          what + ": the refused edit is left default-constructed");
}

app::StructuralEdit hand_edit(bool assign, std::uint32_t task, std::uint32_t resource,
                              std::uint32_t assignment) {
    app::StructuralEdit hand;
    hand.kind = assign ? app::StructuralKind::assign_resource
                       : app::StructuralKind::unassign_resource;
    hand.uid = task;
    hand.resource_uid = resource;
    hand.assignment_uid = assignment;
    return hand;
}

app::PlanInput load_plan(const std::filesystem::path& path) {
    fixture::require_committed(path);
    return app::plan_input_from_document(mpp::load_file(path.string()));
}

void check_refusals() {
    const std::filesystem::path add_path = data_path(kAdd.control);
    const std::filesystem::path remove_path = data_path(kRemove.control);
    const std::filesystem::path assigned_path = data_path(kAdd.project);
    const std::filesystem::path second_path = data_path("wave34-assign-add-second-control.mpp");
    app::PlanInput plan;
    app::PlanInput remove_plan;
    app::PlanInput assigned_plan;
    app::PlanInput second_plan;
    try {
        plan = load_plan(add_path);
        remove_plan = load_plan(remove_path);
        assigned_plan = load_plan(assigned_path);
        second_plan = load_plan(second_path);
    } catch (const std::exception&) {
        check(false, "the refusal fixtures load");
        return;
    }
    const std::string source = add_path.string();
    const std::uint32_t task = kAdd.task_uid;
    const std::uint32_t resource = kAdd.resource_uid;

    /* The key. */
    expect_refused(plan, source, 0, resource, true, "not a saved task", "task uid 0");
    expect_refused(plan, source, 4242, resource, true, "not a saved task", "an unknown task");
    expect_refused(plan, source, task, 0, true, "not a saved resource", "resource uid 0");
    expect_refused(plan, source, task, 4242, true, "not a saved resource", "an unknown resource");
    expect_refused(remove_plan, remove_path.string(), 4242, resource, false, "not a saved task",
                   "remove: an unknown task");

    /* The plan-side rules: each is a shape the pair did not measure. */
    {
        app::PlanInput changed = plan;
        for (app::TaskRow& row : changed.tasks) {
            if (row.uid == task) {
                row.summary = true;
            }
        }
        expect_refused(changed, source, task, resource, true, "summary task", "a summary task");
    }
    {
        app::PlanInput changed = plan;
        for (app::TaskRow& row : changed.tasks) {
            if (row.uid == task) {
                row.percent_complete = 50;
            }
        }
        expect_refused(changed, source, task, resource, true, "has started", "a task at 50 %");
    }
    {
        app::PlanInput changed = plan;
        for (app::TaskRow& row : changed.tasks) {
            if (row.uid == task) {
                row.actual_start = app::date32_from_packed(0x3BF212C0u);
            }
        }
        expect_refused(changed, source, task, resource, true, "has started",
                       "a task with an actual start");
        changed = plan;
        for (app::TaskRow& row : changed.tasks) {
            if (row.uid == task) {
                row.actual_duration_ticks = 4800;
            }
        }
        expect_refused(changed, source, task, resource, true, "has started",
                       "a task with an actual duration");
    }
    {
        app::PlanInput changed = plan;
        for (app::ResourceRow& row : changed.resources) {
            if (row.uid == resource) {
                row.type = core::view::ResourceType::material;
            }
        }
        expect_refused(changed, source, task, resource, true, "not a work resource",
                       "a material resource");
        changed = plan;
        for (app::ResourceRow& row : changed.resources) {
            if (row.uid == resource) {
                row.max_units_percent = 50.0;
            }
        }
        expect_refused(changed, source, task, resource, true, "Max. Units",
                       "a resource under 100 % Max. Units");
    }
    /* W34b-0.1 (R34G3-1 / RC-R1): the rest of the measured fixture shape.  Each
     * case changes one field of the plan, for the add and for the removal, and
     * names the field in the reason (never the generic "not implemented" alone). */
    {
        struct ShapeCase {
            const char* label;
            const char* needle;
            std::function<void(app::TaskRow&)> task_change;
            std::function<void(app::ResourceRow&)> resource_change;
        };
        const std::vector<ShapeCase> cases = {
            {"a task below the top level", "is not top-level",
             [](app::TaskRow& row) { row.outline_level = 1; }, nullptr},
            {"an automatically scheduled task", "automatically scheduled",
             [](app::TaskRow& row) { row.manually_scheduled = false; }, nullptr},
            {"a 3-day task", "not the measured 5 days",
             [](app::TaskRow& row) { row.duration_ticks = 14400; }, nullptr},
            {"a task with its own calendar", "with its own calendar",
             [](app::TaskRow& row) { row.calendar_uid = 3; }, nullptr},
            {"a constrained task", "with a scheduling constraint",
             [](app::TaskRow& row) { row.constraint_type = 2; }, nullptr},
            {"a resource with a standard rate", "standard or overtime rate", nullptr,
             [](app::ResourceRow& row) { row.standard_rate_per_hour = 25.0; }},
            {"a resource with an overtime rate", "standard or overtime rate", nullptr,
             [](app::ResourceRow& row) { row.overtime_rate_per_hour = 37.5; }},
            {"a resource with a cost per use", "has a cost per use", nullptr,
             [](app::ResourceRow& row) { row.cost_per_use = 10.0; }},
            {"a resource accruing at start", "cost accrual is not the default", nullptr,
             [](app::ResourceRow& row) {
                 row.cost_fields_stored = true;
                 row.accrue_at = 1;
             }},
            /* W35-0.2 (R34b-2): a task that stores a Type block. */
            {"a task that stores a task type", "stores a task type",
             [](app::TaskRow& row) { row.type_block_stored = true; }, nullptr},
            /* W35-0.1 (R34b-1): the resource's calendar row, one test per shape. */
            {"a resource calendar based on another calendar",
             "whose calendar is based on another calendar", nullptr,
             [](app::ResourceRow& row) { row.calendar_base_uid = 3; }},
            {"a resource with no calendar row", "whose calendar is based on another calendar",
             nullptr, [](app::ResourceRow& row) { row.calendar_base_uid = -1; }},
            {"a resource calendar with an exception", "has working-time changes or exceptions",
             nullptr, [](app::ResourceRow& row) { row.calendar_stored_blocks = 1; }},
            {"a Standard calendar with an exception", "has working-time changes or exceptions",
             nullptr, [](app::ResourceRow& row) { row.calendar_base_extra_blocks = 1; }},
        };
        for (const ShapeCase& one : cases) {
            for (const bool assign : {true, false}) {
                app::PlanInput changed = assign ? plan : remove_plan;
                const std::uint32_t task_uid = assign ? kAdd.task_uid : kRemove.task_uid;
                const std::uint32_t resource_uid = assign ? kAdd.resource_uid : kRemove.resource_uid;
                for (app::TaskRow& row : changed.tasks) {
                    if (row.uid == task_uid && one.task_change) {
                        one.task_change(row);
                    }
                }
                for (app::ResourceRow& row : changed.resources) {
                    if (row.uid == resource_uid && one.resource_change) {
                        one.resource_change(row);
                    }
                }
                expect_refused(changed, assign ? source : remove_path.string(), task_uid,
                               resource_uid, assign, one.needle,
                               std::string(assign ? "assign: " : "remove: ") + one.label);
                /* push() backstops a hand-built queue with the same rule. */
                app::StructuralEdits edits;
                check(!edits.push(hand_edit(assign, task_uid, resource_uid, kAdd.task_uid + 100),
                                  &changed),
                      std::string("push() refuses ") + (assign ? "an assign" : "a removal") +
                          " on " + one.label);
                check(edits.empty(), std::string("push(): nothing queued for ") + one.label);
            }
        }
    }
    /* W35-0.2 (R34b-2): the removed assignment's own units and work, at the
     * builder and in push().  The unchanged plan removes (the positive control
     * is the measured removal pair above). */
    {
        struct AssignmentCase {
            const char* label;
            const char* needle;
            std::function<void(app::AssignmentRow&)> change;
        };
        const std::vector<AssignmentCase> assignment_cases = {
            {"a removed assignment at 50 % units", "units are not 100%",
             [](app::AssignmentRow& row) { row.units_percent = 50.0; }},
            {"a removed assignment of 24 hours", "not the measured 40 hours",
             [](app::AssignmentRow& row) { row.work_ticks = 14400; }},
        };
        for (const AssignmentCase& one : assignment_cases) {
            app::PlanInput changed = remove_plan;
            bool touched = false;
            for (app::AssignmentRow& row : changed.assignments) {
                if (row.task_uid == kRemove.task_uid && row.resource_uid == kRemove.resource_uid) {
                    one.change(row);
                    touched = true;
                }
            }
            check(touched, std::string("the removal fixture holds the assignment: ") + one.label);
            expect_refused(changed, remove_path.string(), kRemove.task_uid, kRemove.resource_uid,
                           false, one.needle, std::string("remove: ") + one.label);
            app::StructuralEdits edits;
            check(!edits.push(hand_edit(false, kRemove.task_uid, kRemove.resource_uid,
                                        kAdd.task_uid + 100),
                              &changed),
                  std::string("push() refuses a removal of ") + one.label);
            check(edits.empty(), std::string("push(): nothing queued for ") + one.label);
        }
    }
    /* The unmeasured counts: a task that already has a real assignment. */
    expect_refused(assigned_plan, assigned_path.string(), task, resource, true,
                   "already has an assignment", "a task that already has an assignment");
    expect_refused(assigned_plan, assigned_path.string(), task, 1, true,
                   "already has an assignment",
                   "a second resource on an assigned task");
    /* A removal must be the task's one assignment of that resource. */
    expect_refused(plan, source, task, resource, false, "one assignment",
                   "remove: a task with no real assignment");
    expect_refused(remove_plan, remove_path.string(), task, 1, false, "one assignment",
                   "remove: a resource the task is not assigned");
    {
        app::PlanInput two = assigned_plan;
        app::AssignmentRow extra = *find_task_assignment(two, task, true);
        extra.uid = 4000;
        extra.resource_uid = 1;
        extra.resource_uid_raw = 1;
        two.assignments.push_back(extra);
        expect_refused(two, assigned_path.string(), task, resource, false, "one assignment",
                       "remove: a task with two assignments");
    }

    /* The file decides the rest (the writer's dry run). */
    expect_refused(plan, remove_path.string(), task, resource, true,
                   "assignment add: the assignment FixedMeta is not the measured rows / rows shape",
                   "assign against a file whose assignment is already real");
    expect_refused(remove_plan, source, task, resource, false,
                   "assignment remove: the task's 0x0B4004E5 leaf does not name the resource",
                   "remove against a file whose assignment is only a placeholder");
    {
        /* The plan believes the task has no real assignment; the file (the
         * settled append shape) holds one.  The writer would accept that, so the
         * builder refuses it as a stale plan. */
        app::PlanInput stale = second_plan;
        std::vector<app::AssignmentRow> kept;
        for (const app::AssignmentRow& row : stale.assignments) {
            if (!(row.task_uid == task && row.resource_uid != 0)) {
                kept.push_back(row);
            }
        }
        stale.assignments = kept;
        /* The writer takes only a lower uid on the one assigned resource
         * (wave34-assign-add-second), so the stale-plan check is reached with the
         * lowest free resource; the greatest is refused by the writer (W35-0.4,
         * RC-W2). */
        std::uint32_t lowest = 0;
        std::uint32_t highest = 0;
        for (const app::ResourceRow& row : second_plan.resources) {
            const app::AssignmentRow* held = nullptr;
            for (const app::AssignmentRow& a : second_plan.assignments) {
                if (a.task_uid == task && a.resource_uid == row.uid) {
                    held = &a;
                }
            }
            if (held == nullptr && row.uid != 0) {
                lowest = lowest == 0 ? row.uid : std::min(lowest, row.uid);
                highest = std::max(highest, row.uid);
            }
        }
        check(lowest != 0, "the settled-append fixture has a resource the task does not hold");
        expect_refused(stale, second_path.string(), task, lowest, true, "no longer matches",
                       "a plan that shows no assignment on a task the file has assigned");
        check(highest > lowest, "the settled-append fixture has a higher free resource");
        expect_refused(stale, second_path.string(), task, highest, true,
                       "in an order no pair measures",
                       "assign: a higher uid on the one assigned resource (stale plan)");
    }
    /* W35b-1.6 (R35G3-1, RW35-W1): the file the port saved and Project resaved.
     * Its null save settles the assignment tables, so only the save-session bit of
     * the target resource's FixedMeta entry (0x1C to 0x5C) tells it from the
     * measured control; Project's removal from it also clears a Rsc Fixed2Meta
     * flag the port never writes. */
    {
        const std::filesystem::path resaved_path =
            data_path("wave35-g3-assign-remove-port-saved-control-null.mpp");
        app::PlanInput resaved_plan;
        try {
            resaved_plan = load_plan(resaved_path);
        } catch (const std::exception&) {
            check(false, "the resaved port file loads");
            return;
        }
        const char* const needle =
            "the target resource's FixedMeta byte 12 is 0x5C, not the measured 0x1C";
        expect_refused(resaved_plan, resaved_path.string(), 1, 2, false, needle,
                       "remove an assignment from a port-saved file Project resaved");

        /* A hand-built edit passes push() (the plan shows the assignment) and the
         * save's own writer refuses it before anything is written. */
        bool shown = false;
        std::uint32_t fresh_uid = 0;
        for (const app::AssignmentRow& row : resaved_plan.assignments) {
            shown = shown || (row.task_uid == 1 && row.resource_uid == 2);
            fresh_uid = std::max(fresh_uid, row.uid);
        }
        ++fresh_uid;  // push() wants the uid the writer would append, not one in the plan
        check(shown, "the resaved port file shows task 1's assignment to resource 2");
        app::StructuralEdits hand_edits;
        check(hand_edits.push(hand_edit(false, 1, 2, fresh_uid), &resaved_plan),
              "push() accepts the hand-built removal (the plan shows the assignment)");
        const std::filesystem::path out = out_path("app-assign-remove-resaved.mpp");
        std::error_code remove_error;
        std::filesystem::remove(out, remove_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(hand_edits, {}, resaved_path.string(), out.string(), false);
        check(!saved.ok, "the save refuses the removal from the resaved port file");
        check(saved.error.find(needle) != std::string::npos,
              std::string("the save's refusal names the resource flag byte (got \"") +
                  saved.error + "\")");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }
    expect_refused(plan, (std::filesystem::path(APP_TEST_OUT_DIR) / "no-such-file.mpp").string(),
                   task, resource, true, "could not be read", "a missing project file");
    {
        mppwrite::CfbDocument broken = mppwrite::CfbDocument::from_file(source);
        Bytes props = broken.read_stream(kProps);
        broken.replace_stream(kProps, Bytes(props.begin(), props.begin() + 40));
        const std::filesystem::path broken_path = out_path("app-assign-broken-props.mpp");
        broken.save(broken_path.string());
        expect_refused(plan, broken_path.string(), task, resource, true,
                       "the Props stream has no 0x04000001 scalar",
                       "a file whose 114/Props is not the measured shape");
    }
    /* W34b-0.1: a shape the plan cannot show (the task type, effort-driven flag,
     * a resource's base calendar word) is still refused, by the writer's dry run
     * through the builder, because the saved rows must equal the measured ones. */
    for (const bool assign : {true, false}) {
        const std::string path = assign ? source : remove_path.string();
        const app::PlanInput& base_plan = assign ? plan : remove_plan;
        const std::uint32_t task_uid = assign ? kAdd.task_uid : kRemove.task_uid;
        const std::uint32_t resource_uid = assign ? kAdd.resource_uid : kRemove.resource_uid;
        const std::string side = assign ? "assign: " : "remove: ";
        {
            mppwrite::CfbDocument scratch = mppwrite::CfbDocument::from_file(path);
            Bytes fixed = scratch.read_stream(kTaskFixed);
            fixed[48 + 202 * task_uid + 64] ^= 0x01;
            scratch.replace_stream(kTaskFixed, fixed);
            const std::filesystem::path scratch_path =
                out_path(assign ? "app-assign-task-row-add.mpp" : "app-assign-task-row-remove.mpp");
            scratch.save(scratch_path.string());
            expect_refused(base_plan, scratch_path.string(), task_uid, resource_uid, assign,
                           "differs from the measured task row", side + "a task row byte off the measured one");
        }
        {
            mppwrite::CfbDocument scratch = mppwrite::CfbDocument::from_file(path);
            Bytes fixed = scratch.read_stream(kRscFixed);
            fixed[48 + 172 * resource_uid + 154] ^= 0x01;
            scratch.replace_stream(kRscFixed, fixed);
            const std::filesystem::path scratch_path =
                out_path(assign ? "app-assign-rsc-row-add.mpp" : "app-assign-rsc-row-remove.mpp");
            scratch.save(scratch_path.string());
            expect_refused(base_plan, scratch_path.string(), task_uid, resource_uid, assign,
                           "base calendar", side + "a resource calendar word off the measured one");
        }
    }

    /* W35-0.4 (RC-W1): the first-assignment add is measured on a table of three
     * placeholder rows only; a consistent four-row or two-row table is refused
     * by the writer's dry run, and the shell relays its wording. */
    for (const bool grow : {true, false}) {
        mppwrite::CfbDocument scratch = mppwrite::CfbDocument::from_file(source);
        Bytes fixed = scratch.read_stream(kAssnFixed);
        Bytes f2 = scratch.read_stream(kAssnF2);
        Bytes fixed_meta = scratch.read_stream(kAssnMeta);
        Bytes f2_meta = scratch.read_stream(kAssnF2Meta);
        if (grow) {
            const Bytes row(fixed.begin(), fixed.begin() + 110);
            fixed.insert(fixed.end(), row.begin(), row.end());
            const Bytes record(f2.begin(), f2.begin() + 48);
            f2.insert(f2.end(), record.begin(), record.end());
        } else {
            fixed.resize(fixed.size() - 110);
            f2.resize(f2.size() - 48);
        }
        for (std::size_t k = 0; k < 4; ++k) {
            fixed_meta[12 + k] = static_cast<std::uint8_t>((fixed.size() >> (8 * k)) & 0xFFu);
            f2_meta[12 + k] = static_cast<std::uint8_t>((f2.size() >> (8 * k)) & 0xFFu);
        }
        scratch.replace_stream(kAssnFixed, fixed);
        scratch.replace_stream(kAssnF2, f2);
        scratch.replace_stream(kAssnMeta, fixed_meta);
        scratch.replace_stream(kAssnF2Meta, f2_meta);
        const std::filesystem::path scratch_path =
            out_path(grow ? "app-assign-four-rows.mpp" : "app-assign-two-rows.mpp");
        scratch.save(scratch_path.string());
        expect_refused(plan, scratch_path.string(), kAdd.task_uid, kAdd.resource_uid, true,
                       "first-assignment add is measured only on a table of 3",
                       grow ? "assign: a fourth placeholder row" : "assign: only two placeholder rows");
    }

    /* push(): the plan-side backstop for a hand-built queue. */
    const std::uint32_t new_uid = kAdd.task_uid + 100;  // not in the plan
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(true, task, resource, new_uid), nullptr),
              "push() refuses an assign with no plan");
        check(!edits.push(hand_edit(true, 0, resource, new_uid), &plan), "push() refuses task 0");
        check(!edits.push(hand_edit(true, task, 0, new_uid), &plan), "push() refuses resource 0");
        check(!edits.push(hand_edit(true, task, resource, 0), &plan),
              "push() refuses assignment uid 0");
        check(!edits.push(hand_edit(true, 4242, resource, new_uid), &plan),
              "push() refuses an unknown task");
        check(!edits.push(hand_edit(true, task, 4242, new_uid), &plan),
              "push() refuses an unknown resource");
        check(!edits.push(hand_edit(true, task, resource, plan.assignments.front().uid), &plan),
              "push() refuses an assignment uid the plan already uses");
        check(!edits.push(hand_edit(true, task, resource, new_uid), &assigned_plan),
              "push() refuses an assign over a real assignment");
        check(!edits.push(hand_edit(false, task, resource, new_uid), &plan),
              "push() refuses a removal with no real assignment");
        app::PlanInput started = plan;
        for (app::TaskRow& row : started.tasks) {
            row.percent_complete = 100;
        }
        check(!edits.push(hand_edit(true, task, resource, new_uid), &started),
              "push() refuses an assign on a started task");
        check(edits.empty(), "no refused assignment left anything in the queue");
    }

    /* Exclusivity, in both directions and with the other exclusive kinds. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(true, task, resource, new_uid), &plan),
              "push() refuses an assign over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(true, task, resource, new_uid), &plan),
              "an assign queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan),
              "push() refuses another edit over an assign");
        check(!edits.push(hand_edit(true, task, resource, new_uid + 1), &plan),
              "push() refuses a second assign");
        check(!edits.push(hand_edit(false, task, resource, new_uid + 1), &assigned_plan),
              "push() refuses a removal over an assign");
        app::StructuralEdit rename;
        rename.kind = app::StructuralKind::rename_resource;
        rename.uid = 1;
        rename.name = "R2b";
        check(!edits.push(std::move(rename), &plan), "push() refuses a rename over an assign");
        app::StructuralEdit add;
        add.kind = app::StructuralKind::add_resource;
        add.uid = 9;
        add.resource_id = 9;
        add.name = "R9";
        add.created = 0x3d182a12u;
        check(!edits.push(std::move(add), &plan), "push() refuses an add resource over an assign");
        check_size(edits.size(), 1, "only the assign stays queued");
    }

    /* The cascade refuses a start-date edit beside an assignment before it writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_assignment_edit(plan, source, task, resource, true, edit, error),
              "the exclusivity fixture builds its assignment");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the assignment");
        const std::filesystem::path out = out_path("app-assign-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses an assignment with a start-date edit");
        check(saved.error.find("start-date") != std::string::npos,
              "the save's refusal names the start-date edit");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* W34b-1.3 (RC-R3, R34G4-8): a pending table-format or custom-field formula
     * edit reaches the same save through the document-edit hook, so the cascade
     * refuses the combination before the hook or any write; the queue is kept. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_assignment_edit(plan, source, task, resource, true, edit, error),
              "the document-edit fixture builds its assignment");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the document-edit fixture queues its assignment");
        const std::filesystem::path out = out_path("app-assign-document-edit-refused.mpp");
        const std::size_t queued = edits.applied().size();
        int hook_calls = 0;
        for (const bool as_formula : {false, true}) {
            std::error_code fs_error;
            std::filesystem::remove(out, fs_error);
            app::StructuralDocumentEdits request;
            if (as_formula) {
                request.formula.operation = app::CustomFormulaEdit::Operation::set;
                request.formula.field_id = 0x0b400033u;
                request.formula.formula = "[Duration]/240";
            } else {
                request.view.set_date_format = true;
                request.view.date_format = 0x05;
            }
            request.hook = [&hook_calls](mppwrite::CfbDocument&, const app::ViewStateEdits&,
                                         const app::CustomFormulaEdit&) {
                ++hook_calls;
                return app::DocumentEditOutcome{};
            };
            const std::string what = std::string("its assignment with a pending ") +
                                     (as_formula ? "formula" : "table format") + " edit";
            const app::StructuralSaveResult saved = app::save_structural_edits(
                edits, {}, source, out.string(), false, request);
            check(!saved.ok, "the save refuses " + what);
            check(saved.error.find("an assignment edit cannot be combined with a table format") != std::string::npos,
                  "the save's refusal names the combination for " + what + " (got \"" +
                      saved.error + "\")");
            check(hook_calls == 0, "the hook never ran for " + what);
            check(!std::filesystem::exists(out), "the refused save writes no target for " + what);
            check(edits.applied().size() == queued, "the queue is unchanged after " + what);
        }
    }

    /* A file that changed after the edit was queued: the queued assignment uid
     * came from the add control; the same edit saved against a file whose next
     * assignment uid differs is refused before anything is written, as is a file
     * that is no longer the measured shape. */
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(true, task, resource, new_uid), &plan),
              "the stale-uid fixture queues the assignment");
        const std::filesystem::path out = out_path("app-assign-stale.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, source, out.string(), false);
        check(!saved.ok, "the save fails on a queued assignment uid the writer would not use");
        check(saved.error.find("not the one the edit queued") != std::string::npos,
              "the failed save names the uid mismatch (got \"" + saved.error + "\")");
        check(!std::filesystem::exists(out), "the failed save writes no target file");
        check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
              "the failed save leaves no intermediate file");
    }
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_assignment_edit(plan, source, task, resource, true, edit, error),
              "the changed-file fixture builds its assignment");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the changed-file fixture queues it");
        const std::filesystem::path out = out_path("app-assign-changed-file.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, remove_path.string(), out.string(), false);
        check(!saved.ok, "the save fails on a file whose assignment is already real");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
        check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
              "the refused save leaves no intermediate file");
    }
}

// W35-4.1: the lookup both shells use to choose between assigning and removing.
void check_held_assignment_resource() {
    app::PlanInput plan;
    app::AssignmentRow placeholder;
    placeholder.uid = 10;
    placeholder.task_uid = 1;
    placeholder.resource_uid = 0;  // Project's unassigned placeholder
    app::AssignmentRow real;
    real.uid = 11;
    real.task_uid = 2;
    real.resource_uid = 5;
    plan.assignments = {placeholder, real};
    check(app::held_assignment_resource(plan, 1) == 0,
          "a task holding only the placeholder holds no real assignment");
    check(app::held_assignment_resource(plan, 2) == 5,
          "a task's one real assignment names its resource");
    check(app::held_assignment_resource(plan, 3) == 0, "an unknown task holds none");
}

}  // namespace

int main() {
    check_pair(kAdd);
    check_pair(kRemove);
    check_refusals();
    check_held_assignment_resource();
    std::printf("app_edits_assignment_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_assignment_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
