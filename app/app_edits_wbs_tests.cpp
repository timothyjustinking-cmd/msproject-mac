// app/app_edits_wbs_tests.cpp -- the WBS code definition through the app's edit
// model and save cascade against Project's own save pair (wave 34, W34-4.6).
//
// The committed pair (spec/writers/evidence/wave30-wbs.md):
//
//   wave30-wbs-control.mpp   -> wave30-wbs-code.mpp
//       Project > Properties > WBS > Define Code with prefix "W", level 1,
//       ordered two-digit numbers and separator "."; four stored number blocks
//       (the project row "03", tasks 1-3 "01" "02" "03") and two grown records
//       in 114/Props
//
// with a committed null save of the control (the null-save mask source).
//
// This test:
//
//   1. loads the control through the app's reader adapter and builds the edit
//      with make_wbs_code_edit();
//   2. queues it, proves the DISPLAY is unchanged (the plan carries no stored
//      WBS text) and that undo/redo work;
//   3. saves it with the real cascade and compares every stream of the port's
//      save against Project's under the null-save discipline (save_pair::
//      compare_all) with the five owned streams DECLARED changed so a mask that
//      swallowed the change fails `effects_ok`; the effect bytes are counted;
//   4. checks the cascade wrote the target once (a refused save leaves an
//      existing target byte-identical) and left no intermediate file.
//
// IN THE EDIT'S COORDINATES.  114/Props GROWS by 24 bytes in the middle (two
// records), so the per-save-noise bytes after the growth sit 24 bytes later in
// Project's save and the port's than in the control.  save_pair masks by byte
// position, so the control and the null save are given the same two zero-filled
// insertions at the ends of the two records' payloads (identical in both, so
// the mask is exactly the control-vs-null difference) and the pair is compared
// in the edit's coordinates; the record ids, lengths and the resulting size are
// asserted against Project's own save.  tests/roundtrip_wbs.cpp does the same.
//
// It also checks every refusal the edit model owes the user (brief rule 2: one
// pair is one data point, so only the measured population is enabled): another
// task count, a summary or nested task, an unsaved task, other task uids, a
// file that already stores WBS text or a code definition, a damaged Task table,
// a missing file, push()'s plan-side backstop and the edit's exclusivity (in the
// queue and in the cascade).
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w34-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w34-app --config Release --target app_edits_wbs_tests
//   ctest --test-dir build\w34-app -C Release -R "^app_edits_wbs$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
// "table.h" would find core/view/table.h; the app include path lists
// core/view first), the same rule as app_edits_cell_bold_tests.cpp.
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
const char* const kProps = "114/Props";
const char* const kMarker = "Props14";

/* The five streams the measured edit owns. */
const std::array<const char*, 5> kDeclared{
    {kTaskMeta, kTaskVarMeta, kTaskVar2, kProps, kMarker}};

const char* const kControl = "wave30-wbs-control.mpp";
const char* const kNull = "wave30-wbs-control-null.mpp";
const char* const kProject = "wave30-wbs-code.mpp";

/* The total of the pair's effect bytes, in the edit's coordinates: FixedMeta 8,
 * VarMeta 108 + 48 tail, Var2Data 40 tail, 114/Props 11 and Props14 1 (the
 * figure tests/roundtrip_wbs.cpp prints as "pair totals ... effects=216" over
 * the whole file; every other stream has none). */
constexpr std::size_t kPairEffects = 216;

std::filesystem::path data_path(const std::string& name) {
    return std::filesystem::path(APP_TEST_DATA_DIR) / name;
}

std::filesystem::path out_path(const std::string& name) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    return std::filesystem::path(APP_TEST_OUT_DIR) / name;
}

Bytes stream_or_empty(const mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

std::uint32_t u32_at(const Bytes& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
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
            left.tasks[index].outline_number != right.tasks[index].outline_number) {
            return false;
        }
    }
    return true;
}

/* 114/Props with the two records' growth inserted (see the file comment). */
Bytes grown_props(const Bytes& raw) {
    std::vector<std::pair<std::size_t, std::size_t>> insertions; /* at, extra */
    std::size_t at = 16;
    while (at + 12 <= raw.size()) {
        const std::uint32_t length = u32_at(raw, at);
        const std::uint32_t id = u32_at(raw, at + 4);
        if (id == 0x0240138Bu) {
            insertions.emplace_back(at + 12 + length, 20);
        } else if (id == 0x024013BEu) {
            insertions.emplace_back(at + 12 + length, 4);
        }
        at += 12 + length;
    }
    std::sort(insertions.begin(), insertions.end());
    Bytes out = raw;
    for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(it->first), it->second, 0);
    }
    return out;
}

mppwrite::CfbDocument in_edit_coordinates(mppwrite::CfbDocument document) {
    document.replace_stream(kProps, grown_props(document.read_stream(kProps)));
    return document;
}

/* --- the measured pair ----------------------------------------------------- */

void check_pair() {
    const std::string label = "wbs-code";
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
    check_size(plan.tasks.size(), 3, label + ": the control holds the three measured tasks");

    /* --- make_wbs_code_edit ----------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_wbs_code_edit(plan, control_path.string(), edit, edit_error);
    check(built, label + ": make_wbs_code_edit accepts the measured project");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::define_wbs_code, label + ": the built edit has the kind");
    check_size(edit.uid, 0, label + ": the edit names no row");

    /* --- push + display + undo/redo --------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the edit is queued");
    check(same_plan(edits.apply(plan), plan),
          label + ": the display is unchanged (the plan carries no stored WBS text)");
    check(edits.undo(), label + ": the edit undoes");
    check(edits.empty(), label + ": undo empties the queue");
    check(edits.redo(), label + ": the edit redoes");
    check_size(edits.size(), 1, label + ": redo restores the one edit");

    /* --- save through the real cascade ------------------------------------ */
    const std::filesystem::path out = out_path("app-wbs-code.mpp");
    std::error_code error;
    std::filesystem::remove(out, error);
    const app::StructuralSaveResult saved =
        app::save_structural_edits(edits, {}, control_path.string(), out.string(), false);
    check(saved.ok, label + ": the save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.wbs_codes_written, 1, label + ": the cascade wrote the code");
    check(saved.api.find(" save_wbs_code") != std::string::npos,
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

    /* The grown records' ids and sizes, against Project's own save. */
    check_size(grown_props(control.read_stream(kProps)).size(),
               project.read_stream(kProps).size(),
               label + ": the grown control has Project's 114/Props length");
    check_size(output.read_stream(kProps).size(), project.read_stream(kProps).size(),
               label + ": the port's 114/Props has Project's length");

    /* Every stream, under the null-save discipline, in the edit's coordinates. */
    const mppwrite::CfbDocument shifted_control = in_edit_coordinates(control);
    const mppwrite::CfbDocument shifted_null = in_edit_coordinates(null_save);
    const Context context;
    save_pair::compare_all(context, label, shifted_control, shifted_null, project, output,
                           nullptr, 0, kDeclared.data(), kDeclared.size());

    const std::vector<save_pair::TailNormalisation> no_normalisation;
    std::size_t effects = 0;
    for (const char* path : kDeclared) {
        const save_pair::Kind kind = save_pair::kind_for_path(path);
        const save_pair::Verdict verdict = save_pair::compare(
            kind, stream_or_empty(shifted_control, path), stream_or_empty(shifted_null, path),
            stream_or_empty(project, path), stream_or_empty(output, path), true,
            save_pair::normalisation_for(no_normalisation, path));
        effects += verdict.effects + verdict.tail_effects;
        check_size(verdict.mismatches, 0, label + ": " + path + ": equals Project's save");
    }
    std::printf("   %s: declared effect bytes = %llu\n", label.c_str(),
                static_cast<unsigned long long>(effects));
    check_size(effects, kPairEffects, label + ": the pair's declared effect bytes");

    /* Every stream OUTSIDE the declared ones is byte-identical to the control. */
    for (const std::string& path : control.stream_paths()) {
        bool owned = false;
        for (const char* declared : kDeclared) {
            owned = owned || path == declared;
        }
        if (owned) {
            continue;
        }
        check(stream_or_empty(output, path) == stream_or_empty(control, path),
              label + ": " + path + ": untouched by the edit");
    }

    /* The reader reads the same tasks back. */
    try {
        const app::PlanInput reread = load_plan(out);
        check(same_plan(reread, plan), label + ": the reader reads the same plan back");
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
    const bool built = app::make_wbs_code_edit(plan, source, edit, error);
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

app::StructuralEdit hand_edit() {
    app::StructuralEdit hand;
    hand.kind = app::StructuralKind::define_wbs_code;
    return hand;
}

bool edits_accept(const app::PlanInput& plan_in) {
    app::StructuralEdits edits;
    return edits.push(hand_edit(), &plan_in);
}

void check_refusals() {
    const std::filesystem::path control_path = data_path(kControl);
    const std::filesystem::path coded_path = data_path(kProject);
    app::PlanInput plan;
    app::PlanInput coded_plan;
    try {
        plan = load_plan(control_path);
        coded_plan = load_plan(coded_path);
    } catch (const std::exception&) {
        check(false, "the refusal fixtures load");
        return;
    }
    const std::string source = control_path.string();
    const char* const not_implemented = "not implemented in this port";

    /* The population (brief rule 2: one pair, one project shape). */
    {
        app::PlanInput fewer = plan;
        fewer.tasks.pop_back();
        expect_refused(fewer, source, "2 tasks", "a project with two tasks");
    }
    {
        app::PlanInput more = plan;
        more.tasks.push_back(plan.tasks.back());
        more.tasks.back().uid = 4;
        expect_refused(more, source, "4 tasks", "a project with four tasks");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[1].summary = true;
        expect_refused(changed, source, "an outline", "a summary task");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[2].outline_level = 1;
        expect_refused(changed, source, "an outline", "a nested task");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[0].file_row = 0;
        expect_refused(changed, source, "pending", "a task that is not yet saved");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[0].uid = 7;
        expect_refused(changed, source, "UniqueIDs 1, 2 and 3", "tasks with other UniqueIDs");
    }
    {
        app::PlanInput changed = plan;
        changed.tasks[1].uid = changed.tasks[0].uid;
        expect_refused(changed, source, "UniqueIDs 1, 2 and 3", "a repeated UniqueID");
    }

    /* R34G4-5: the numbers follow the UniqueID, so the ID and the stored row
     * order must be the UniqueID order too. */
    {
        app::PlanInput changed = plan;
        std::swap(changed.tasks[0].id, changed.tasks[1].id);
        expect_refused(changed, source, "ID differs from its UniqueID",
                       "tasks whose IDs are not their UniqueIDs");
        check(!edits_accept(changed), "push() refuses tasks whose IDs are not their UniqueIDs");
    }
    {
        app::PlanInput changed = plan;
        std::swap(changed.tasks[0].file_row, changed.tasks[1].file_row);
        expect_refused(changed, source, "stored out of UniqueID order",
                       "tasks stored in another row order");
        check(!edits_accept(changed), "push() refuses tasks stored in another row order");
    }

    /* The file decides the rest (the writer's dry run).  A plan that agrees with
     * the three flat tasks but a file that is a nested outline, or whose row
     * words differ, is refused by the writer. */
    {
        const std::filesystem::path nested_path = data_path("wave28-outline-indent.mpp");
        expect_refused(plan, nested_path.string(), "is a summary",
                       "a file that holds a nested three-task outline");
    }
    {
        mppwrite::CfbDocument reordered = mppwrite::CfbDocument::from_file(source);
        Bytes fixed = reordered.read_stream(kTaskFixed);
        for (int i = 0; i < 4; ++i) {
            fixed[48 + 202 * 1 + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(i == 0 ? 2 : 0);
            fixed[48 + 202 * 2 + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(i == 0 ? 1 : 0);
        }
        reordered.replace_stream(kTaskFixed, fixed);
        const std::filesystem::path reordered_path = out_path("app-wbs-reordered-ids.mpp");
        reordered.save(reordered_path.string());
        expect_refused(plan, reordered_path.string(), "has ID 2 but UniqueID 1",
                       "a file whose IDs are not its UniqueIDs");
    }
    expect_refused(coded_plan, coded_path.string(), not_implemented,
                   "a file that already stores WBS text and a code definition");
    expect_refused(plan, (std::filesystem::path(APP_TEST_OUT_DIR) / "no-such-file.mpp").string(),
                   "could not be read", "a missing project file");
    {
        mppwrite::CfbDocument broken = mppwrite::CfbDocument::from_file(source);
        Bytes fixed = broken.read_stream(kTaskFixed);
        fixed.resize(fixed.size() - 202);
        broken.replace_stream(kTaskFixed, fixed);
        const std::filesystem::path broken_path = out_path("app-wbs-broken-fixed.mpp");
        broken.save(broken_path.string());
        expect_refused(plan, broken_path.string(), not_implemented,
                       "a file whose Task table lost a row");
    }
    {
        mppwrite::CfbDocument broken = mppwrite::CfbDocument::from_file(source);
        Bytes props = broken.read_stream(kProps);
        std::size_t at = 16;
        while (at + 12 <= props.size()) {
            const std::uint32_t length = u32_at(props, at);
            if (u32_at(props, at + 4) == 0x0240138Bu) {
                props[at + 12] = 1; /* a non-empty code mask */
                break;
            }
            at += 12 + length;
        }
        broken.replace_stream(kProps, props);
        const std::filesystem::path broken_path = out_path("app-wbs-existing-mask.mpp");
        broken.save(broken_path.string());
        expect_refused(plan, broken_path.string(), not_implemented,
                       "a file with a non-empty code mask record");
    }

    /* push(): the plan-side backstop for a hand-built queue. */
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(), nullptr), "push() refuses a WBS edit with no plan");
        app::StructuralEdit with_row = hand_edit();
        with_row.uid = 1;
        check(!edits.push(std::move(with_row), &plan), "push() refuses a WBS edit that names a row");
        app::StructuralEdit with_name = hand_edit();
        with_name.name = "W";
        check(!edits.push(std::move(with_name), &plan),
              "push() refuses a WBS edit that carries a prefix");
        app::PlanInput fewer = plan;
        fewer.tasks.pop_back();
        check(!edits.push(hand_edit(), &fewer), "push() refuses another task count");
        app::PlanInput nested = plan;
        nested.tasks[1].summary = true;
        check(!edits.push(hand_edit(), &nested), "push() refuses a summary");
        check(edits.empty(), "no refused WBS edit left anything in the queue");
    }

    /* Exclusivity, in both directions and with the other exclusive kinds. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(), &plan), "push() refuses a WBS edit over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(), &plan), "a WBS edit queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan), "push() refuses another edit over a WBS edit");
        check(!edits.push(hand_edit(), &plan), "push() refuses a second WBS edit");
        app::StructuralEdit rename;
        rename.kind = app::StructuralKind::rename_resource;
        rename.uid = 1;
        rename.name = "A longer resource name";
        check(!edits.push(std::move(rename), &plan), "push() refuses a rename over a WBS edit");
        check_size(edits.size(), 1, "only the WBS edit stays queued");
    }

    /* The cascade refuses a start-date edit beside a WBS edit before it writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_wbs_code_edit(plan, source, edit, error),
              "the exclusivity fixture builds its WBS edit");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the edit");
        const std::filesystem::path out = out_path("app-wbs-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses a WBS edit with a start-date edit");
        check(saved.error.find("start-date") != std::string::npos,
              "the save's refusal names the start-date edit");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* A file that changed after the edit was queued: the same edit saved against
     * Project's already-coded file is refused, and an EXISTING target is left
     * byte-identical (the code is written to the target once, after the chain). */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_wbs_code_edit(plan, source, edit, error),
              "the changed-file fixture builds its WBS edit");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the changed-file fixture queues it");
        const std::filesystem::path out = out_path("app-wbs-changed-file.mpp");
        std::error_code fs_error;
        std::filesystem::copy_file(control_path, out,
                                   std::filesystem::copy_options::overwrite_existing, fs_error);
        check(!fs_error, "the existing target is staged");
        const Bytes target_before = mppwrite::CfbDocument::from_file(out.string()).to_bytes();
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, coded_path.string(), out.string(), false);
        check(!saved.ok, "the save fails on a file that already stores the code");
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
    std::printf("app_edits_wbs_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_wbs_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
