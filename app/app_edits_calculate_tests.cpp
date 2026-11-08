// app/app_edits_calculate_tests.cpp -- Calculate Project (F9) through the app's
// edit model and save cascade against Project's own save pair (wave 35,
// W35-1.3).
//
// The committed pair (spec/writers/evidence/wave35-calculate-unlinked.md):
//
//   wave35-calculate-unlinked-control.mpp -> wave35-calculate-unlinked.mpp
//       the project row plus two unlinked automatic leaves, one Duration
//       changed under manual calculation, then Project > Calculate Project and
//       save
//
// with a committed null save of the control (the null-save mask source).
//
// This test:
//
//   1. loads the control through the app's reader adapter and builds the edit
//      with make_calculate_edit();
//   2. proves the words the app computed itself (the project Duration and
//      Finish, and every leaf's slack and late dates, from the file's rows and
//      the project calendar) EQUAL the words Project wrote in its own result;
//   3. queues the edit, proves the display is unchanged and that undo/redo work;
//   4. saves it with the real cascade and compares every stream of the port's
//      save against Project's under the null-save discipline (save_pair::
//      compare_all) with the owned streams DECLARED changed so a mask that
//      swallowed the change fails `effects_ok`;
//   5. checks every refusal the edit model owes the user (brief rule 2: one
//      pair is one data point, so only the measured shape is enabled): the other
//      three committed pairs (linked, lag, snet), another task count, a summary,
//      a manual, constrained or progressed task, a task calendar, an already
//      calculated file, a damaged file and a missing one, push()'s plan-side
//      backstop and the edit's exclusivity (in the queue and in the cascade).
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w35-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w35-app --config Release --target app_edits_calculate_tests
//   ctest --test-dir build\w35-app -C Release -R "^app_edits_calculate$" --output-on-failure

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "fixture_guard.h"
#include "layout_model.h"
// core/mppwrite/outline.h pins the core/mppwrite/table.h header (a bare
// "table.h" would find core/view/table.h), the same rule as
// app_edits_wbs_tests.cpp.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "row_writer.h"
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
        /* This pair has no KnownGap entry: any expected-failure report is a
         * failure of the test itself. */
        ::check(false, "unexpected KnownGap report: " + what +
                           (unexpected_pass ? " (gap gone)" : ""));
    }
};

const char* const kTaskMeta = "114/TBkndTask/FixedMeta";
const char* const kTaskVarMeta = "114/TBkndTask/VarMeta";
const char* const kTaskVar2 = "114/TBkndTask/Var2Data";
const char* const kTaskFixed = "114/TBkndTask/FixedData";
const char* const kTaskFixed2 = "114/TBkndTask/Fixed2Data";
const char* const kTaskFixed2Meta = "114/TBkndTask/Fixed2Meta";
const char* const kProps = "114/Props";
const char* const kMarker = "Props14";
const char* const kSummary = "\005DocumentSummaryInformation";

/* The eight streams Project's F9 save changes in the unlinked pair. */
const std::array<const char*, 8> kDeclared{{kProps, kTaskFixed, kTaskMeta, kTaskFixed2,
                                            kTaskVar2, kTaskVarMeta, kSummary, kMarker}};

const char* const kControl = "wave35-calculate-unlinked-control.mpp";
const char* const kNull = "wave35-calculate-unlinked-control-null.mpp";
const char* const kProject = "wave35-calculate-unlinked.mpp";

std::filesystem::path data_path(const std::string& name) {
    return std::filesystem::path(APP_TEST_DATA_DIR) / name;
}

std::filesystem::path out_path(const std::string& name) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    return std::filesystem::path(APP_TEST_OUT_DIR) / name;
}

/* A leaf whose Duration changed also changes its placeholder assignment's planned work (an
 * IEEE double at TBkndAssn +20 of 110-byte rows, ticks x 100), as Project saves it. */
void set_planned_work(mppwrite::CfbDocument& doc, std::uint32_t task_uid, std::uint32_t ticks) {
    const std::string stream = "114/TBkndAssn/FixedData";
    Bytes assn = doc.read_stream(stream);
    for (std::size_t base = 0; base + 110 <= assn.size(); base += 110) {
        const std::uint32_t uid = static_cast<std::uint32_t>(assn[base + 4]) |
                                  (static_cast<std::uint32_t>(assn[base + 5]) << 8) |
                                  (static_cast<std::uint32_t>(assn[base + 6]) << 16) |
                                  (static_cast<std::uint32_t>(assn[base + 7]) << 24);
        if (uid == task_uid) {
            const double work = static_cast<double>(ticks) * 100.0;
            std::memcpy(&assn[base + 20], &work, sizeof work);
        }
    }
    doc.replace_stream(stream, assn);
}

app::PlanInput load_plan(const std::filesystem::path& path) {
    fixture::require_committed(path);
    return app::plan_input_from_document(mpp::load_file(path.string()));
}

bool same_plan(const app::PlanInput& left, const app::PlanInput& right) {
    if (left.tasks.size() != right.tasks.size() ||
        left.resources.size() != right.resources.size() ||
        left.assignments.size() != right.assignments.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.tasks.size(); ++index) {
        if (left.tasks[index].uid != right.tasks[index].uid ||
            left.tasks[index].name != right.tasks[index].name ||
            left.tasks[index].file_row != right.tasks[index].file_row ||
            left.tasks[index].start != right.tasks[index].start ||
            left.tasks[index].finish != right.tasks[index].finish) {
            return false;
        }
    }
    return true;
}

/* --- the measured pair ----------------------------------------------------- */

void check_pair() {
    const std::string label = "calculate-unlinked";
    const std::filesystem::path control_path = data_path(kControl);
    const std::filesystem::path result_path = data_path(kProject);
    const std::filesystem::path null_path = data_path(kNull);
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);
    check(null_path.filename().string() == save_pair::null_fixture_name(kControl),
          label + ": the null save is the control's committed null fixture");

    app::PlanInput plan;
    try {
        plan = load_plan(control_path);
    } catch (const std::exception& error) {
        check(false, label + ": the control loads through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    check_size(plan.tasks.size(), 2, label + ": the control holds the two measured leaves");

    /* --- make_calculate_edit ---------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_calculate_edit(plan, control_path.string(), edit, edit_error);
    check(built, label + ": make_calculate_edit accepts the measured project");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::calculate_project,
          label + ": the built edit has the kind");
    check_size(edit.uid, 0, label + ": the edit names no row");
    check_size(edit.slack.size(), 2, label + ": the edit carries one entry per leaf");

    /* --- the computed words equal Project's own ---------------------------- */
    const mppwrite::CfbDocument project_doc =
        mppwrite::CfbDocument::from_file(result_path.string());
    const mppwrite::TaskTable result_rows = mppwrite::TaskTable::from_document(project_doc);
    check(edit.summary_duration == result_rows.u32(0, mppwrite::task_row::kDuration),
          label + ": the computed project Duration is Project's");
    check(static_cast<std::uint32_t>(edit.restored_finish) ==
              result_rows.u32(0, mppwrite::task_row::kFinish),
          label + ": the computed project Finish is Project's");
    for (const app::OutlineSlackEntry& entry : edit.slack) {
        const std::size_t row = result_rows.row_for_uid(entry.uid);
        const std::string who = label + ": uid " + std::to_string(entry.uid);
        check(entry.free_slack == result_rows.u32(row, 74), who + ": free slack is Project's");
        check(entry.start_slack == result_rows.u32(row, mppwrite::task_row::kStartSlack),
              who + ": start slack is Project's");
        check(entry.total_slack == result_rows.u32(row, 190), who + ": total slack is Project's");
        check(static_cast<std::uint32_t>(entry.late_start) ==
                  result_rows.u32(row, mppwrite::task_row::kStart3),
              who + ": late start is Project's");
        check(static_cast<std::uint32_t>(entry.late_finish) ==
                  result_rows.u32(row, mppwrite::task_row::kFinish3),
              who + ": late finish is Project's");
    }

    /* --- push + display + undo/redo --------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the edit is queued");
    check(same_plan(edits.apply(plan), plan),
          label + ": the display is unchanged (the plan holds the leaf dates already)");
    check(edits.undo(), label + ": the edit undoes");
    check(edits.empty(), label + ": undo empties the queue");
    check(edits.redo(), label + ": the edit redoes");
    check_size(edits.size(), 1, label + ": redo restores the one edit");

    /* --- save through the real cascade ------------------------------------ */
    const std::filesystem::path out = out_path("app-calculate-unlinked.mpp");
    std::error_code error;
    std::filesystem::remove(out, error);
    const app::StructuralSaveResult saved =
        app::save_structural_edits(edits, {}, control_path.string(), out.string(), false);
    check(saved.ok, label + ": the save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.calculations_written, 1, label + ": the cascade wrote the calculation");
    check(saved.api.find(" save_calculate") != std::string::npos,
          label + ": the save's api names the writer");
    check(saved.warnings.empty(), label + ": the save has no warnings");
    check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
          label + ": the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument null_save;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        null_save = mppwrite::CfbDocument::from_file(null_path.string());
        output = mppwrite::CfbDocument::from_file(out.string());
    } catch (const std::exception& open_error) {
        check(false, label + ": the control, null save and port save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* Every stream, under the null-save discipline. */
    const Context context;
    save_pair::compare_all(context, label, control, null_save, project_doc, output, nullptr, 0,
                           kDeclared.data(), kDeclared.size());

    /* Every stream OUTSIDE the declared ones is byte-identical to the control
     * (the intermediate chain left nothing else changed). */
    for (const std::string& path : control.stream_paths()) {
        bool owned = false;
        for (const char* declared : kDeclared) {
            owned = owned || path == declared;
        }
        if (owned) {
            continue;
        }
        const Bytes before = control.read_stream(path);
        const Bytes after = output.has_stream(path) ? output.read_stream(path) : Bytes{};
        check(before == after, label + ": " + path + ": untouched by the edit");
    }

    /* The reader reads the same tasks back, and the file is no longer pending:
     * a second Calculate Project is refused. */
    try {
        const app::PlanInput reread = load_plan(out);
        check(same_plan(reread, plan), label + ": the reader reads the same plan back");
        app::StructuralEdit again;
        std::string again_error;
        check(!app::make_calculate_edit(reread, out.string(), again, again_error),
              label + ": the calculated file is refused a second time");
        check(again_error.find("not pending") != std::string::npos,
              label + ": the second refusal names the pending flag (got \"" + again_error + "\")");
    } catch (const std::exception& read_error) {
        check(false, label + ": the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* --- refusals -------------------------------------------------------------- */

void expect_refused(const app::PlanInput& plan, const std::string& source, const char* needle,
                    const std::string& what) {
    app::StructuralEdit edit;
    std::string error;
    const bool built = app::make_calculate_edit(plan, source, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what.c_str());
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
    check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0,
          what + ": the refused edit is left default-constructed");
}

app::StructuralEdit hand_edit(const app::PlanInput& plan) {
    app::StructuralEdit hand;
    hand.kind = app::StructuralKind::calculate_project;
    hand.summary_duration = 1;
    hand.restored_finish = 1;
    for (const app::TaskRow& task : plan.tasks) {
        app::OutlineSlackEntry entry;
        entry.uid = task.uid;
        hand.slack.push_back(entry);
    }
    return hand;
}

void check_refusals() {
    const std::filesystem::path control_path = data_path(kControl);
    app::PlanInput plan;
    try {
        plan = load_plan(control_path);
    } catch (const std::exception&) {
        check(false, "the refusal fixtures load");
        return;
    }
    const std::string source = control_path.string();

    /* The other three committed pairs: their plan or file differs. */
    for (const char* pair : {"linked", "lag", "snet"}) {
        const std::filesystem::path path =
            data_path(std::string("wave35-calculate-") + pair + "-control.mpp");
        app::PlanInput other;
        try {
            other = load_plan(path);
        } catch (const std::exception&) {
            check(false, std::string("the ") + pair + " control loads");
            continue;
        }
        app::StructuralEdit edit;
        std::string error;
        check(!app::make_calculate_edit(other, path.string(), edit, error),
              std::string("the ") + pair + " pair is refused");
        check(error.find("not implemented in this port") != std::string::npos,
              std::string("the ") + pair + " refusal uses the decision-9 text (got \"" + error +
                  "\")");
    }

    /* The plan-side population. */
    {
        app::PlanInput fewer = plan;
        fewer.tasks.pop_back();
        expect_refused(fewer, source, "1 tasks", "a project with one task");
    }
    {
        app::PlanInput more = plan;
        more.tasks.push_back(plan.tasks.back());
        more.tasks.back().uid = 3;
        expect_refused(more, source, "3 tasks", "a project with three tasks");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[1].summary = true;
        expect_refused(changed, source, "an outline", "a summary task");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[1].outline_level = 1;
        expect_refused(changed, source, "an outline", "a nested task");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[0].file_row = 0;
        expect_refused(changed, source, "pending", "a task that is not yet saved");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[0].manually_scheduled = true;
        expect_refused(changed, source, "manually scheduled", "a manually scheduled task");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[0].constraint_type = 4;
        expect_refused(changed, source, "a constraint", "a constrained task");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[1].percent_complete = 50;
        expect_refused(changed, source, "progress", "a progressed task");
    }
    {
        /* W35b-0.2 (R35G1-1): the plan carries the deadline; the file layer is below. */
        app::PlanInput changed = plan;
        changed.tasks[1].deadline = changed.tasks[1].finish;
        expect_refused(changed, source, "for a task with a deadline", "a task with a deadline");
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(changed), &changed),
              "push() refuses a Calculate edit over a plan with a deadline");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[1].calendar_uid = 3;
        expect_refused(changed, source, "its own calendar", "a task calendar");
    }
    /* W35b-0.3 (R35G1-2): the three pending recalculations no pair measured.  The pair
     * lengthens the critical leaf, leaves both critical flags as they were, and every
     * leaf starts at the project start. */
    {
        /* The critical path moves: the plan says leaf 1 is critical, leaf 2 is longer. */
        app::PlanInput changed = plan;
        changed.tasks[1].duration_ticks = 72000;
        changed.tasks[1].finish = app::date32_from_packed(0x3C0427D8u);
        expect_refused(changed, source, "moves the critical path",
                       "a recalculation that moves the critical path (plan)");
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(changed), &changed),
              "push() refuses a Calculate edit over a plan that moves the critical path");
    }
    {
        /* The project would shorten: the stored project finish is later than every leaf. */
        app::PlanInput changed = plan;
        changed.project_finish = app::date32_from_packed(0x3BFE27D8u);
        expect_refused(changed, source, "would shorten the project",
                       "a recalculation that shortens the project (plan)");
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(changed), &changed),
              "push() refuses a Calculate edit over a plan that shortens the project");
    }
    {
        /* A leaf that does not start at the project start. */
        app::PlanInput changed = plan;
        changed.tasks[1].start = app::date32_from_packed(0x3BF312C0u);
        expect_refused(changed, source, "does not start at the project start",
                       "a leaf that does not start at the project start (plan)");
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(changed), &changed),
              "push() refuses a Calculate edit over a plan with a moved leaf start");
    }
    /* W35b-0.4 (RW35-W2): the pending recalculation must have been caused by a Duration
     * change alone, so each leaf's Finish is its Start plus its Duration on the measured
     * calendar (Monday to Friday, 08:00 to 17:00, whole 8-hour days). */
    {
        /* A Duration edit whose Finish Project has not moved. */
        app::PlanInput changed = plan;
        changed.tasks[1].duration_ticks = 19200;
        expect_refused(changed, source, "is not its start plus its duration",
                       "a leaf duration without its finish (plan)");
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(changed), &changed),
              "push() refuses a Calculate edit over a plan whose leaf duration and finish differ");
    }
    {
        /* A Finish edit. */
        app::PlanInput changed = plan;
        changed.tasks[1].finish = app::date32_from_packed(0x3BF527D8u);
        expect_refused(changed, source, "is not its start plus its duration",
                       "a leaf finish edit (plan)");
    }
    {
        /* A Start edit that moved the project start and both leaf starts together, so the
         * leaves still start at the project start and the Finish dates are the old ones. */
        app::PlanInput changed = plan;
        changed.project_start = app::date32_from_packed(0x3BF312C0u);
        changed.tasks[0].start = app::date32_from_packed(0x3BF312C0u);
        changed.tasks[1].start = app::date32_from_packed(0x3BF312C0u);
        expect_refused(changed, source, "is not its start plus its duration",
                       "a leaf start edit (plan)");
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(changed), &changed),
              "push() refuses a Calculate edit over a plan whose start edit left the finish");
    }
    {
        app::PlanInput changed = plan;
        app::LinkRow link;
        link.predecessor_uid = plan.tasks[0].uid;
        link.successor_uid = plan.tasks[1].uid;
        changed.links.push_back(link);
        expect_refused(changed, source, "links", "a plan with a link");
    }

    /* The file decides the rest. */
    {
        const std::filesystem::path done_path = data_path(kProject);
        const app::PlanInput done_plan = load_plan(done_path);
        expect_refused(done_plan, done_path.string(), "not pending",
                       "a file Project already calculated");
    }
    expect_refused(plan, (std::filesystem::path(APP_TEST_OUT_DIR) / "no-such-file.mpp").string(),
                   "could not be read", "a missing project file");
    {
        mppwrite::CfbDocument broken = mppwrite::CfbDocument::from_file(source);
        Bytes fixed = broken.read_stream(kTaskFixed);
        fixed.resize(fixed.size() - 202);
        broken.replace_stream(kTaskFixed, fixed);
        const std::filesystem::path broken_path = out_path("app-calculate-broken-fixed.mpp");
        broken.save(broken_path.string());
        expect_refused(plan, broken_path.string(), "not implemented in this port",
                       "a file whose Task table lost a row");
    }
    {
        /* W35b-0.2 (R35G1-1): the plan has no deadline, the FILE does (the plan passed
         * here is the clean control's), so the writer's own check names it. */
        mppwrite::CfbDocument dated = mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskTable rows = mppwrite::TaskTable::from_document(dated);
        rows.set_u32(2, mppwrite::task_row::kDeadline, 0x3BF212C0u);
        rows.store_into(dated);
        const std::filesystem::path dated_path = out_path("app-calculate-deadline-leaf.mpp");
        dated.save(dated_path.string());
        expect_refused(plan, dated_path.string(), "carries a deadline",
                       "a file whose leaf carries a deadline");
    }
    {
        /* W35b-0.3: the FILE holds the unmeasured recalculation, the plan is the clean
         * control's, so the writer's own checks name it. */
        mppwrite::CfbDocument moved = mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskTable rows = mppwrite::TaskTable::from_document(moved);
        rows.set_u32(2, mppwrite::task_row::kDuration, 72000);
        rows.set_u32(2, mppwrite::task_row::kFinish, 0x3C0427D8u);
        rows.store_into(moved);
        set_planned_work(moved, 2, 72000);
        const std::filesystem::path moved_path = out_path("app-calculate-critical-moves.mpp");
        moved.save(moved_path.string());
        expect_refused(plan, moved_path.string(), "moves the critical path",
                       "a file whose recalculation moves the critical path");
    }
    {
        mppwrite::CfbDocument shorter = mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskTable rows = mppwrite::TaskTable::from_document(shorter);
        rows.set_u32(0, mppwrite::task_row::kFinish, 0x3BFE27D8u);
        rows.store_into(shorter);
        const std::filesystem::path shorter_path = out_path("app-calculate-shortens.mpp");
        shorter.save(shorter_path.string());
        expect_refused(plan, shorter_path.string(), "would shorten the project",
                       "a file whose recalculation shortens the project");
    }
    {
        mppwrite::CfbDocument early = mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskTable rows = mppwrite::TaskTable::from_document(early);
        rows.set_u32(2, mppwrite::task_row::kStart, 0x3BF312C0u);
        rows.store_into(early);
        const std::filesystem::path early_path = out_path("app-calculate-leaf-start.mpp");
        early.save(early_path.string());
        expect_refused(plan, early_path.string(), "starts at a date other than the project start",
                       "a file whose leaf does not start at the project start");
    }
    {
        /* W35b-0.4: the FILE holds a leaf whose Duration, Finish or Start was edited and not
         * recalculated; the plan is the clean control's, so the writer's own check names it. */
        mppwrite::CfbDocument longer = mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskTable rows = mppwrite::TaskTable::from_document(longer);
        rows.set_u32(2, mppwrite::task_row::kDuration, 19200);
        rows.store_into(longer);
        set_planned_work(longer, 2, 19200);
        const std::filesystem::path longer_path = out_path("app-calculate-duration-edit.mpp");
        longer.save(longer_path.string());
        expect_refused(plan, longer_path.string(), "is not its start plus its duration",
                       "a file whose leaf duration has no matching finish");
    }
    {
        mppwrite::CfbDocument moved = mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskTable rows = mppwrite::TaskTable::from_document(moved);
        rows.set_u32(2, mppwrite::task_row::kFinish, 0x3BF527D8u);
        rows.store_into(moved);
        const std::filesystem::path moved_path = out_path("app-calculate-finish-edit.mpp");
        moved.save(moved_path.string());
        expect_refused(plan, moved_path.string(), "is not its start plus its duration",
                       "a file whose leaf finish was edited");
    }
    {
        mppwrite::CfbDocument shifted = mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskTable rows = mppwrite::TaskTable::from_document(shifted);
        for (std::size_t row = 0; row < 3; ++row) {
            rows.set_u32(row, mppwrite::task_row::kStart, 0x3BF312C0u);
        }
        rows.store_into(shifted);
        const std::filesystem::path shifted_path = out_path("app-calculate-start-edit.mpp");
        shifted.save(shifted_path.string());
        expect_refused(plan, shifted_path.string(), "is not its start plus its duration",
                       "a file whose project and leaf starts were edited");
    }
    {
        mppwrite::CfbDocument manual = mppwrite::CfbDocument::from_file(source);
        Bytes meta = manual.read_stream(kTaskFixed2Meta);
        meta[16 + 96 * 4 + 8] |= 0x80;
        manual.replace_stream(kTaskFixed2Meta, meta);
        const std::filesystem::path manual_path = out_path("app-calculate-manual-leaf.mpp");
        manual.save(manual_path.string());
        expect_refused(plan, manual_path.string(), "is manually scheduled",
                       "a file whose leaf is manually scheduled");
    }

    /* push(): the plan-side backstop for a hand-built queue. */
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(plan), nullptr), "push() refuses a Calculate edit with no plan");
        app::StructuralEdit with_row = hand_edit(plan);
        with_row.uid = 1;
        check(!edits.push(std::move(with_row), &plan),
              "push() refuses a Calculate edit that names a row");
        app::StructuralEdit with_name = hand_edit(plan);
        with_name.name = "x";
        check(!edits.push(std::move(with_name), &plan),
              "push() refuses a Calculate edit that carries a name");
        app::StructuralEdit short_slack = hand_edit(plan);
        short_slack.slack.pop_back();
        check(!edits.push(std::move(short_slack), &plan),
              "push() refuses a Calculate edit with a missing leaf result");
        app::PlanInput fewer = plan;
        fewer.tasks.pop_back();
        check(!edits.push(hand_edit(fewer), &fewer), "push() refuses another task count");
        app::PlanInput nested = plan;
        nested.tasks[1].summary = true;
        check(!edits.push(hand_edit(nested), &nested), "push() refuses a summary");
        check(edits.empty(), "no refused Calculate edit left anything in the queue");
    }

    /* Exclusivity, in both directions and with the other exclusive kinds. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(plan), &plan),
              "push() refuses a Calculate edit over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(plan), &plan), "a Calculate edit queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan),
              "push() refuses another edit over a Calculate edit");
        check(!edits.push(hand_edit(plan), &plan), "push() refuses a second Calculate edit");
        check_size(edits.size(), 1, "only the Calculate edit stays queued");
    }

    /* The cascade refuses a start-date edit beside a Calculate edit before it
     * writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_calculate_edit(plan, source, edit, error),
              "the exclusivity fixture builds its Calculate edit");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the edit");
        const std::filesystem::path out = out_path("app-calculate-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses a Calculate edit with a start-date edit");
        check(saved.error.find("start-date") != std::string::npos,
              "the save's refusal names the start-date edit (got \"" + saved.error + "\")");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* A pending table-format or custom-field formula edit reaches the same save
     * through the document-edit hook, so the cascade refuses the combination
     * before the hook or any write; the queue is kept. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_calculate_edit(plan, source, edit, error),
              "the document-edit fixture builds its Calculate edit");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan),
              "the document-edit fixture queues its Calculate edit");
        const std::filesystem::path out = out_path("app-calculate-document-edit-refused.mpp");
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
            const std::string what = std::string("its Calculate edit with a pending ") +
                                     (as_formula ? "formula" : "table format") + " edit";
            const app::StructuralSaveResult saved =
                app::save_structural_edits(edits, {}, source, out.string(), false, request);
            check(!saved.ok, "the save refuses " + what);
            check(saved.error.find("a Calculate Project edit cannot be combined with a table "
                                   "format") != std::string::npos,
                  "the save's refusal names the combination for " + what + " (got \"" +
                      saved.error + "\")");
            check(hook_calls == 0, "the hook never ran for " + what);
            check(!std::filesystem::exists(out), "the refused save writes no target for " + what);
            check(edits.applied().size() == queued, "the queue is unchanged after " + what);
        }
    }

    /* A file that changed after the edit was queued: the same edit saved against
     * Project's already-calculated file is refused, and an EXISTING target is
     * left byte-identical. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_calculate_edit(plan, source, edit, error),
              "the changed-file fixture builds its Calculate edit");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the changed-file fixture queues it");
        const std::filesystem::path out = out_path("app-calculate-changed-file.mpp");
        std::error_code fs_error;
        std::filesystem::copy_file(control_path, out,
                                   std::filesystem::copy_options::overwrite_existing, fs_error);
        check(!fs_error, "the existing target is staged");
        const Bytes target_before = mppwrite::CfbDocument::from_file(out.string()).to_bytes();
        const app::StructuralSaveResult saved = app::save_structural_edits(
            edits, {}, data_path(kProject).string(), out.string(), false);
        check(!saved.ok, "the save fails on a file that is already calculated");
        check(mppwrite::CfbDocument::from_file(out.string()).to_bytes() == target_before,
              "the refused save leaves the existing target byte-identical");
        check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
              "the refused save leaves no intermediate file");
    }
}

}  // namespace

int main() {
    check_pair();
    check_refusals();
    std::printf("app_edits_calculate_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_calculate_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
