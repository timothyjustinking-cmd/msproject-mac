// app/app_edits_show_level_tests.cpp -- View > Outline > Show > Level N persisted
// through the app's edit model and save cascade against Project's own save pairs
// (wave 35, W35-2.3).
//
// The committed pairs (spec/writers/view-state.md section 3, each with a null
// save of its control):
//
//   wave35-viewstate-outline-level1-control.mpp       -> ...-outline-level1.mpp
//       Level 1 on the five-task outline (levels 1,2,2,1,1)
//   wave35-viewstate-outline-deep-level1-control.mpp  -> ...-outline-deep-level1.mpp
//   wave35-viewstate-outline-deep-level2-control.mpp  -> ...-outline-deep-level2.mpp
//       Level 1 and Level 2 on the six-task three-level outline (1,2,3,3,2,1)
//   wave35-viewstate-outline-level2-control.mpp       -> ...-outline-level2.mpp
//   wave35-viewstate-outline-all-control.mpp          -> ...-outline-all.mpp
//       Level 2 and Show All on the five-task outline: nothing is hidden and
//       Project's save differs from its control by the marker alone, so the edit
//       model refuses them as "nothing to persist" (core/mppwrite/
//       roundtrip_viewstate_outline covers the writer on them).
//
// For each effect pair this test loads the control through the app's reader
// adapter, builds the edit with make_show_level_edit(), queues it and proves the
// DISPLAY is unchanged (the shell shows the level for the session) and that
// undo/redo work, saves it with the real cascade and compares every stream of the
// port's save with Project's under the null-save discipline
// (save_pair::compare_all), the owned streams declared changed so a mask that
// swallowed the change fails.
//
// It also checks every refusal the edit model owes the user (brief rule 1: only
// the measured shapes are enabled): a level other than 1 or 2, a level that hides
// nothing, a pending task, another outline shape, a file that already hides rows
// or is not the measured view, push()'s plan-side backstop, the edit's
// exclusivity (in the queue and in the cascade) and a save-time refusal on a file
// that changed after the edit was queued.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w35-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w35-app --config Release --target app_edits_show_level_tests
//   ctest --test-dir build\w35-app -C Release -R "^app_edits_show_level$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
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
        /* These pairs have no KnownGap entry: any expected-failure report is a
         * failure of the test itself. */
        ::check(false, "unexpected KnownGap report: " + what +
                           (unexpected_pass ? " (gap gone)" : ""));
    }
};

const char* const kMeta = "114/TBkndTask/FixedMeta";
const char* const kEdlVar2 = "214/CEdl/Var2Data";
const char* const kEdlMeta = "214/CEdl/VarMeta";
const char* const kProps = "214/Props";
const char* const kMarker = "Props14";

constexpr std::size_t kEdlSizeAt = 11402;  // the 0x07000100 scalar's value in 214/Props

struct Pair {
    const char* name;
    int level;
    std::size_t hidden;
    std::size_t collapsed;
};

std::vector<Pair> pairs() {
    return {
        {"outline-level1", 1, 2, 1},
        {"outline-deep-level1", 1, 4, 1},
        {"outline-deep-level2", 2, 2, 1},
    };
}

std::filesystem::path data_path(const std::string& name) {
    return std::filesystem::path(APP_TEST_DATA_DIR) / name;
}

std::filesystem::path out_path(const std::string& name) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    return std::filesystem::path(APP_TEST_OUT_DIR) / name;
}

std::string control_name(const std::string& stem) {
    return "wave35-viewstate-" + stem + "-control.mpp";
}
std::string null_name(const std::string& stem) {
    return "wave35-viewstate-" + stem + "-control-null.mpp";
}
std::string edit_name(const std::string& stem) { return "wave35-viewstate-" + stem + ".mpp"; }

Bytes stream_or_empty(const mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

std::uint32_t u32_at(const Bytes& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void put_u32_at(Bytes& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[o + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

/*
 * The two CEdl size words hold the CEdl Var2Data length, which includes the
 * save-path record 0x26400031 (the full path of the saving file).  Project's null
 * and edit saves are made under other file names than the control, so their size
 * words carry the path-length difference as noise the byte mask only partly
 * covers.  The port keeps the control's path record, so give Project's saves the
 * size their CEdl would have with the control's path record (the same idea as the
 * normalisation save_pair_noise.h applies to the path record inside the stream).
 * See core/mppwrite/viewstate_outline.h.
 */
std::size_t path_record_bytes(const Bytes& edl) {
    const std::size_t count = u32_at(edl, 16);
    std::size_t at = 20;
    for (std::size_t i = 0; i < count && at + 12 <= edl.size(); ++i) {
        const std::size_t length = u32_at(edl, at);
        if (u32_at(edl, at + 4) == 0x26400031u) {
            return length;
        }
        at += 12 + length + (length % 2);
    }
    return 0;
}

mppwrite::CfbDocument with_control_path_size(const std::filesystem::path& file,
                                             std::size_t control_path) {
    mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(file.string());
    const Bytes edl = document.read_stream(kEdlVar2);
    const std::size_t size = edl.size() - path_record_bytes(edl) + control_path;
    Bytes meta = document.read_stream(kEdlMeta);
    Bytes props = document.read_stream(kProps);
    // R35G2-9: Project's own words must be its CEdl length before they are replaced, or the
    // normalisation would hide a difference of its own.
    if (u32_at(meta, 0x14) != edl.size() || u32_at(props, kEdlSizeAt) != edl.size()) {
        throw std::runtime_error(
            "with_control_path_size: " + file.string() + " holds CEdl size words " +
            std::to_string(u32_at(meta, 0x14)) + " (VarMeta +0x14) and " +
            std::to_string(u32_at(props, kEdlSizeAt)) + " (Props), not its CEdl length " +
            std::to_string(edl.size()));
    }
    put_u32_at(meta, 0x14, static_cast<std::uint32_t>(size));
    document.replace_stream(kEdlMeta, meta);
    put_u32_at(props, kEdlSizeAt, static_cast<std::uint32_t>(size));
    document.replace_stream(kProps, props);
    return document;
}

app::PlanInput load_plan(const std::filesystem::path& path) {
    fixture::require_committed(path);
    return app::plan_input_from_document(mpp::load_file(path.string()));
}

/* The same plan: tasks (uid, name, file row), resources and assignments. */
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
            left.tasks[index].outline_level != right.tasks[index].outline_level) {
            return false;
        }
    }
    return true;
}

/* --- the measured pairs ---------------------------------------------------- */

void check_pair(const Pair& p) {
    const std::string label = p.name;
    const std::string stem = p.name;
    const std::filesystem::path control_path = data_path(control_name(stem));
    const std::filesystem::path result_path = data_path(edit_name(stem));
    const std::filesystem::path null_path = data_path(null_name(stem));
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);
    check(null_path.filename().string() == save_pair::null_fixture_name(control_name(stem)),
          label + ": the null save is the control's committed null fixture");

    app::PlanInput plan;
    try {
        plan = load_plan(control_path);
    } catch (const std::exception& error) {
        check(false, label + ": the control loads through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }

    /* --- make_show_level_edit ---------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built =
        app::make_show_level_edit(plan, control_path.string(), p.level, edit, edit_error);
    check(built, label + ": make_show_level_edit accepts the measured level");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::show_level, label + ": the built edit has the kind");
    check(edit.show_level == p.level, label + ": the edit carries the level");
    check(edit.outline_uids.size() == edit.outline_levels.size() && !edit.outline_uids.empty(),
          label + ": the edit carries the file's tasks and their levels");
    check(edit.outline_levels.front() == 0, label + ": the first task is at the top level");

    /* --- push + display + undo/redo --------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the edit is queued");
    check(same_plan(edits.apply(plan), plan),
          label + ": the display is unchanged (the shell shows the level for the session)");
    check(edits.undo(), label + ": the edit undoes");
    check(edits.empty(), label + ": undo empties the queue");
    check(edits.redo(), label + ": the edit redoes");
    check_size(edits.size(), 1, label + ": redo restores the one edit");

    /* --- save through the real cascade ------------------------------------ */
    const std::filesystem::path out = out_path(std::string("app-") + p.name + ".mpp");
    std::error_code error;
    std::filesystem::remove(out, error);
    const app::StructuralSaveResult saved =
        app::save_structural_edits(edits, {}, control_path.string(), out.string(), false);
    check(saved.ok, label + ": the save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.outlines_written, 1, label + ": the cascade wrote the outline level");
    check(saved.api.find(" save_view_outline") != std::string::npos,
          label + ": the save's api names the writer");
    const std::string counts = std::to_string(p.hidden) + " rows hidden, " +
                               std::to_string(p.collapsed) + " summaries collapsed";
    bool noted = false;
    for (const std::string& note : saved.notes) {
        noted = noted || note.find(counts) != std::string::npos;
    }
    check(noted, label + ": the save's note reports " + counts);
    check(saved.warnings.empty(), label + ": the save has no warnings");
    check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
          label + ": the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        output = mppwrite::CfbDocument::from_file(out.string());
    } catch (const std::exception& open_error) {
        check(false, label + ": the control and port save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }
    const std::size_t control_path_bytes = path_record_bytes(control.read_stream(kEdlVar2));
    mppwrite::CfbDocument null_save;
    mppwrite::CfbDocument project;
    try {
        null_save = with_control_path_size(null_path, control_path_bytes);
        project = with_control_path_size(result_path, control_path_bytes);
    } catch (const std::exception& open_error) {
        check(false, label + ": the null save and Project's edit save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* Every stream, under the null-save discipline (the marker first: the
     * leading entries are the declared ones). */
    const std::array<const char*, 5> declared{{kMarker, kMeta, kEdlVar2, kEdlMeta, kProps}};
    const Context context;
    save_pair::compare_all(context, label, control, null_save, project, output, nullptr, 0,
                           declared.data(), declared.size());

    /* The size words are the written stream's own length. */
    const Bytes edl = output.read_stream(kEdlVar2);
    check_size(u32_at(output.read_stream(kEdlMeta), 0x14), edl.size(),
               label + ": the VarMeta size word is the CEdl length");
    check_size(u32_at(output.read_stream(kProps), kEdlSizeAt), edl.size(),
               label + ": the Props size word is the CEdl length");

    /* Every stream OUTSIDE the owned ones is byte-identical to the control. */
    for (const std::string& path : control.stream_paths()) {
        if (path == kMeta || path == kEdlVar2 || path == kEdlMeta || path == kProps ||
            path == kMarker) {
            continue;
        }
        check(stream_or_empty(output, path) == stream_or_empty(control, path),
              label + ": " + path + ": untouched by the edit");
    }
    try {
        const app::PlanInput reread = load_plan(out);
        check(same_plan(reread, plan), label + ": the reader reads the same plan back");
    } catch (const std::exception& read_error) {
        check(false, label + ": the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* --- refusals -------------------------------------------------------------- */

void expect_refused(const app::PlanInput& plan, const std::string& source, int level,
                    const char* needle, const std::string& what) {
    app::StructuralEdit edit;
    std::string error;
    const bool built = app::make_show_level_edit(plan, source, level, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what.c_str());
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
    check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0 && edit.show_level == 0 &&
              edit.outline_uids.empty(),
          what + ": the refused edit is left default-constructed");
}

app::TaskRow* task_by_uid(app::PlanInput& plan, std::uint32_t uid) {
    for (app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

app::StructuralEdit hand_edit(int level, const std::vector<std::uint32_t>& uids,
                              const std::vector<std::uint32_t>& levels) {
    app::StructuralEdit hand;
    hand.kind = app::StructuralKind::show_level;
    hand.show_level = level;
    hand.outline_uids = uids;
    hand.outline_levels = levels;
    return hand;
}

void check_refusals() {
    const std::filesystem::path control_path = data_path(control_name("outline-level1"));
    const std::filesystem::path collapsed_path = data_path(edit_name("outline-level1"));
    app::PlanInput plan;
    try {
        plan = load_plan(control_path);
    } catch (const std::exception&) {
        check(false, "the refusal fixtures load");
        return;
    }
    const std::string source = control_path.string();
    const std::vector<std::uint32_t> uids{1, 2, 3, 4, 5};
    const std::vector<std::uint32_t> levels{0, 1, 1, 0, 0};

    /* The level (brief rule 1: one pair per shape). */
    expect_refused(plan, source, 0, "Show Level 0 is not implemented", "Show Level 0");
    expect_refused(plan, source, 3, "Show Level 3 is not implemented", "Show Level 3");
    expect_refused(plan, source, 4, "Show Level 4 is not implemented", "Show Level 4");
    expect_refused(plan, source, -1, "Show Level -1 is not implemented", "a negative level");
    expect_refused(plan, source, 2, "hides nothing on this outline",
                   "Level 2 on the five-task outline (nothing is hidden)");

    /* The plan: saved rows and the measured outline. */
    {
        app::PlanInput pending = plan;
        task_by_uid(pending, 5)->file_row = 0;
        expect_refused(pending, source, 1, "while a task is pending", "a pending task");
    }
    {
        app::PlanInput fewer = plan;
        for (std::size_t index = 0; index < fewer.tasks.size(); ++index) {
            if (fewer.tasks[index].uid == 5) {
                fewer.tasks.erase(fewer.tasks.begin() + static_cast<std::ptrdiff_t>(index));
                break;
            }
        }
        expect_refused(fewer, source, 1, "outline of 4 tasks is not one the pairs measure",
                       "four rows");
    }
    {
        app::PlanInput deeper = plan;
        task_by_uid(deeper, 3)->outline_level = 2;
        expect_refused(deeper, source, 1, "is not one the pairs measure", "another outline shape");
    }

    /* W35b-1.5 (RW35-R2): a grid edit the plan carries and the file does not.  The
     * outline is the file's, so the writer's dry run accepts it; the builder still
     * refuses, because the persisted view would describe an unsaved plan. */
    {
        app::PlanInput renamed = plan;
        task_by_uid(renamed, 2)->name += "x";
        expect_refused(renamed, source, 1,
                       "carries unsaved edits (UniqueID 2 Name differs from the file)",
                       "Show Level with a renamed task");
        expect_refused(renamed, source, 1, "save or undo the grid edits first",
                       "Show Level with a renamed task asks to save or undo");
        app::PlanInput retimed = plan;
        task_by_uid(retimed, 3)->duration_ticks += 480;
        expect_refused(retimed, source, 1, "UniqueID 3 Duration differs from the file",
                       "Show Level with a typed Duration");
        app::PlanInput flipped = plan;
        task_by_uid(flipped, 1)->summary = !task_by_uid(flipped, 1)->summary;
        expect_refused(flipped, source, 1, "UniqueID 1 Summary differs from the file",
                       "Show Level with a flipped Summary");
    }

    /* The file decides the rest (the writer's dry run). */
    expect_refused(plan, collapsed_path.string(), 1, "already collapsed or hidden",
                   "a file that already hides rows");
    /* W35c-1.10: the Resource Sheet file is also a file whose tasks are not the plan's, but the
     * builder's own view check (the applied view is not the Gantt Chart) now speaks first; the
     * writer's task-row text is pinned on a file with the measured view and another task count
     * (a plan of the six-task measured outline over the five-task control). */
    expect_refused(plan, data_path("wave31-resource-sheet.mpp").string(), 1,
                   "the view Show Level would persist on is not the measured one",
                   "a file whose tasks and view are not the plan's");
    {
        app::PlanInput six = plan;
        const int six_levels[] = {0, 1, 2, 2, 1};
        for (std::uint32_t uid = 1; uid <= 5; ++uid) {
            task_by_uid(six, uid)->outline_level = six_levels[uid - 1];
        }
        app::TaskRow extra = *task_by_uid(six, 5);
        extra.uid = 6;
        extra.id = 6;
        extra.file_row = task_by_uid(six, 5)->file_row + 1;
        extra.outline_level = 0;
        six.tasks.push_back(extra);
        expect_refused(six, source, 1, "task rows (with the project row)",
                       "a file whose tasks are not the plan's");
    }
    /* W35b-1.1 (R35G2-1): Project saves that differ from the control only in their
     * view state.  Each has the same five tasks as the plan, so only the writer's
     * view-state pins can refuse it, each with its own reason. */
    expect_refused(plan, data_path(edit_name("table-cost")).string(), 1,
                   "the applied table is \"Cost\", not the measured Entry table",
                   "R35G2-1: a file with the Cost table applied");
    expect_refused(plan, data_path(edit_name("table-schedule")).string(), 1,
                   "the applied table is \"Schedule\", not the measured Entry table",
                   "R35G2-1: a file with the Schedule table applied");
    expect_refused(plan, data_path(edit_name("zoom-entire")).string(), 1,
                   "outside its sort spec (digest mismatch",
                   "R35G2-1: a file with a zoomed timescale");
    expect_refused(plan, data_path(edit_name("sort-name-asc")).string(), 1,
                   "the file already carries a sort (the spec at block + 40",
                   "R35G2-1: a file with a persisted sort");
    /* W35c-1.10 (R35bG1-3): the builder refuses the same four files at its own call site
     * (require_measured_outline_view) with its own prefix, before the writer's dry run, so
     * removing either check leaves one of these two sets red. */
    for (const char* name : {"table-cost", "table-schedule", "zoom-entire", "sort-name-asc"}) {
        expect_refused(plan, data_path(edit_name(name)).string(), 1,
                       "the view Show Level would persist on is not the measured one",
                       std::string("the builder's own view check on ") + name);
    }
    expect_refused(plan, (std::filesystem::path(APP_TEST_OUT_DIR) / "no-such-file.mpp").string(),
                   1, "could not be read", "a missing project file");

    /* push(): the plan-side backstop for a hand-built queue. */
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(1, uids, levels), nullptr),
              "push() refuses a level with no plan");
        check(!edits.push(hand_edit(3, uids, levels), &plan), "push() refuses Level 3");
        check(!edits.push(hand_edit(0, uids, levels), &plan), "push() refuses Level 0");
        check(!edits.push(hand_edit(2, uids, levels), &plan),
              "push() refuses a level that hides nothing");
        check(!edits.push(hand_edit(1, {1, 2, 3, 5, 4}, levels), &plan),
              "push() refuses rows that are not the file's");
        check(!edits.push(hand_edit(1, uids, {0, 1, 1, 1, 0}), &plan),
              "push() refuses levels that are not the plan's");
        app::StructuralEdit with_row = hand_edit(1, uids, levels);
        with_row.uid = 3;
        check(!edits.push(with_row, &plan), "push() refuses a level that names a row");
        app::StructuralEdit named = hand_edit(1, uids, levels);
        named.name = "x";
        check(!edits.push(named, &plan), "push() refuses a level that carries a name");
        check(edits.empty(), "no refused level left anything in the queue");
        check(edits.push(hand_edit(1, uids, levels), &plan),
              "push() accepts the hand-built measured level");
    }

    /* W35b-3.1 (R35G2-5): push() checks the outline is one the pairs measure, from
     * the plan and from the edit's own level vector.  Each hand-built edit carries
     * exactly the plan's rows, so only the shape check can refuse it. */
    {
        app::PlanInput three_deep = plan;
        task_by_uid(three_deep, 3)->outline_level = 2;
        app::PlanInput four_deep = plan;
        task_by_uid(four_deep, 3)->outline_level = 2;
        task_by_uid(four_deep, 4)->outline_level = 3;
        const auto refused = [&uids](int level, const app::PlanInput& on,
                                     const std::vector<std::uint32_t>& rows_levels,
                                     const std::string& what) {
            app::StructuralEdits fresh;
            check(!fresh.push(hand_edit(level, uids, rows_levels), &on), what);
            check(fresh.empty(), what + ": nothing was queued");
        };
        refused(1, three_deep, {0, 1, 2, 0, 0},
                "R35G2-5: push() refuses Level 1 on a three-level-deep outline {0,1,2,0,0}");
        /* W35c-2.16 (R35bG3-3): Level 3 is over the cap (kOutlineMeasuredMaxLevel = 2),
         * which push() checks first, so this case proves the cap and not the shape guard.
         * Level 2 on the same outline is in range and hides the level-3 and level-4 rows,
         * so only the shape guard refuses it. */
        refused(3, four_deep, {0, 1, 2, 3, 0},
                "R35G2-5: push() refuses Level 3 on a four-level outline {0,1,2,3,0} "
                "(the level cap, not the shape guard)");
        refused(2, four_deep, {0, 1, 2, 3, 0},
                "R35bG3-3: push() refuses Level 2 on a four-level outline {0,1,2,3,0} "
                "(in range, so only the shape guard refuses it)");
        refused(1, four_deep, {0, 1, 2, 3, 0},
                "R35G2-5: push() refuses Level 1 on a four-level outline {0,1,2,3,0}");
    }

    /* Exclusivity, in both directions. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(1, uids, levels), &plan),
              "push() refuses a level over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(1, uids, levels), &plan), "a level queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan),
              "push() refuses another edit over a level");
        check(!edits.push(hand_edit(1, uids, levels), &plan), "push() refuses a second level");
        check_size(edits.size(), 1, "only the level stays queued");
    }

    /* The cascade refuses a start-date edit beside a level before it writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_show_level_edit(plan, source, 1, edit, error),
              "the exclusivity fixture builds its level");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the level");
        const std::filesystem::path out = out_path("app-outline-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses a level with a start-date edit");
        check(saved.error.find("a Show Level edit cannot be combined with a start-date") !=
                  std::string::npos,
              "the save's refusal names the start-date edit (got \"" + saved.error + "\")");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* A pending table-format or formula edit reaches the same save through the
     * document-edit hook; the cascade refuses it before the hook or any write. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_show_level_edit(plan, source, 1, edit, error),
              "the document-edit fixture builds its level");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the document-edit fixture queues its level");
        const std::filesystem::path out = out_path("app-outline-document-edit-refused.mpp");
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
            const std::string what = std::string("its level with a pending ") +
                                     (as_formula ? "formula" : "table format") + " edit";
            const app::StructuralSaveResult saved =
                app::save_structural_edits(edits, {}, source, out.string(), false, request);
            check(!saved.ok, "the save refuses " + what);
            check(saved.error.find("a Show Level edit cannot be combined with a table format") !=
                      std::string::npos,
                  "the save's refusal names the combination for " + what + " (got \"" +
                      saved.error + "\")");
            check(hook_calls == 0, "the hook never ran for " + what);
            check(!std::filesystem::exists(out), "the refused save writes no target for " + what);
            check(edits.applied().size() == queued, "the queue is unchanged after " + what);
        }
    }

    /* A file that changed after the edit was queued: the same edit saved against
     * Project's already-collapsed file is refused before anything is written. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_show_level_edit(plan, source, 1, edit, error),
              "the changed-file fixture builds its level");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the changed-file fixture queues it");
        const std::filesystem::path out = out_path("app-outline-changed-file.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, collapsed_path.string(), out.string(), false);
        check(!saved.ok, "the save fails on a file that already hides rows");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
        check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
              "the refused save leaves no intermediate file");
    }

    /* R35G2-1 at save time: the level queued on the control, the file replaced by a
     * Project save with another table or a zoom, is refused by the writer (not by
     * the queue) and writes nothing.  W35c-2.13 (R35bG1-5): each fixture asserts its own
     * needle, so a row-count, UniqueID or FixedMeta refusal from the same writer no longer
     * passes for the view-state one. */
    struct ViewStateCase {
        const char* stem;
        const char* needle;
    };
    for (const ViewStateCase& view_case :
         {ViewStateCase{"table-cost", "the applied table is \"Cost\", not the measured Entry table"},
          ViewStateCase{"zoom-entire", "outside its sort spec (digest mismatch"},
          ViewStateCase{"sort-name-asc",
                        "the file already carries a sort (the spec at block + 40"}}) {
        const char* stem = view_case.stem;
        app::StructuralEdit edit;
        std::string error;
        check(app::make_show_level_edit(plan, source, 1, edit, error),
              std::string("the view-state fixture builds its level: ") + stem);
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan),
              std::string("the view-state fixture queues it: ") + stem);
        const std::filesystem::path out =
            out_path(std::string("app-outline-view-state-") + stem + ".mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved = app::save_structural_edits(
            edits, {}, data_path(edit_name(stem)).string(), out.string(), false);
        check(!saved.ok, std::string("the save refuses a file whose view state is ") + stem);
        check(saved.error.find("view outline write") != std::string::npos,
              std::string("R35G2-1: the save's refusal is the writer's for ") + stem +
                  " (got \"" + saved.error + "\")");
        check(saved.error.find(view_case.needle) != std::string::npos,
              std::string("R35bG1-5: the save's refusal names the view-state reason for ") +
                  stem + " (got \"" + saved.error + "\")");
        check(!std::filesystem::exists(out),
              std::string("the refused save writes no target for ") + stem);
    }
}

}  // namespace

int main() {
    for (const Pair& p : pairs()) {
        check_pair(p);
    }
    check_refusals();
    std::printf("app_edits_show_level_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_show_level_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
