// app/app_edits_sort_tests.cpp -- Project > Sort persisted through the app's edit
// model and save cascade against Project's own save pairs (wave 35, W35-2.1).
//
// The committed pairs (spec/writers/view-state.md, each with a null save of its
// control):
//
//   wave35-viewstate-sort-name-desc-control.mpp     -> ...-sort-name-desc.mpp
//   wave35-viewstate-sort-name-asc-control.mpp      -> ...-sort-name-asc.mpp
//   wave35-viewstate-sort-duration-desc-control.mpp -> ...-sort-duration-desc.mpp
//       Project > Sort on ONE key, Outline = True, Renumber = False, on the
//       five-task three-level project
//
// For each pair this test:
//
//   1. loads the control through the app's reader adapter and builds the edit with
//      make_sort_edit(); the order the edit computes must be the order Project
//      saved;
//   2. queues it and proves the DISPLAY is unchanged (the shell shows the sort for
//      the session) and that undo/redo work;
//   3. saves it with the real cascade and compares every stream of the port's save
//      with Project's under the null-save discipline (save_pair::compare_all), the
//      owned streams declared changed so a mask that swallowed the change fails.
//
// It also checks every refusal the edit model owes the user (brief rule 1: only
// the measured shapes are enabled): two keys, ID / Start / Finish / other keys,
// Outline = False, Renumber = True, a changed row count, a pending task, names
// and durations the pairs do not support, a file that is already sorted or is not
// the measured view, push()'s plan-side backstop, the edit's exclusivity (in the
// queue and in the cascade) and a save-time refusal on a file that changed after
// the edit was queued.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w35-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w35-app --config Release --target app_edits_sort_tests
//   ctest --test-dir build\w35-app -C Release -R "^app_edits_sort$" --output-on-failure

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

const char* const kViewVar2 = "214/CV_iew/Var2Data";
const char* const kEdlVar2 = "214/CEdl/Var2Data";
const char* const kMarker = "Props14";

constexpr std::uint32_t kFieldName = 0x0B40000Eu;
constexpr std::uint32_t kFieldDuration = 0x0B400507u;

struct Pair {
    const char* name;
    app::SortKeyKind kind;
    bool ascending;
    std::uint32_t field_id;
    std::vector<std::uint32_t> sorted;
    bool order_changes;
};

const std::vector<std::uint32_t> kBase{1, 2, 3, 4, 5};

std::vector<Pair> pairs() {
    return {
        {"sort-name-desc", app::SortKeyKind::name, false, kFieldName, {5, 4, 1, 3, 2}, true},
        {"sort-name-asc", app::SortKeyKind::name, true, kFieldName, {1, 2, 3, 4, 5}, false},
        {"sort-duration-desc", app::SortKeyKind::duration, false, kFieldDuration,
         {4, 1, 2, 3, 5}, true},
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

std::string control_of(const Pair& p) {
    return std::string("wave35-viewstate-") + p.name + "-control.mpp";
}
std::string null_of(const Pair& p) {
    return std::string("wave35-viewstate-") + p.name + "-control-null.mpp";
}
std::string edit_of(const Pair& p) { return std::string("wave35-viewstate-") + p.name + ".mpp"; }

Bytes stream_or_empty(const mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
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
            left.tasks[index].file_row != right.tasks[index].file_row) {
            return false;
        }
    }
    return true;
}

std::vector<app::SortKeyRequest> one_key(app::SortKeyKind kind, bool ascending) {
    app::SortKeyRequest key;
    key.kind = kind;
    key.ascending = ascending;
    return {key};
}

/* --- the measured pairs ---------------------------------------------------- */

void check_pair(const Pair& p) {
    const std::string label = p.name;
    const std::filesystem::path control_path = data_path(control_of(p));
    const std::filesystem::path result_path = data_path(edit_of(p));
    const std::filesystem::path null_path = data_path(null_of(p));
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);
    check(null_path.filename().string() == save_pair::null_fixture_name(control_of(p)),
          label + ": the null save is the control's committed null fixture");

    app::PlanInput plan;
    try {
        plan = load_plan(control_path);
    } catch (const std::exception& error) {
        check(false, label + ": the control loads through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }

    /* --- make_sort_edit ---------------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_sort_edit(plan, control_path.string(),
                                           one_key(p.kind, p.ascending), true, false, edit,
                                           edit_error);
    check(built, label + ": make_sort_edit accepts the measured sort");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::sort_view, label + ": the built edit has the kind");
    check_size(edit.field_id, p.field_id, label + ": the edit names the key's field");
    check(edit.ascending == p.ascending, label + ": the edit has the key's direction");
    check(edit.sort_base_order == kBase, label + ": the base order is the file's");
    check(edit.sort_order == p.sorted, label + ": the order the edit computes is the order "
                                                  "Project saved");

    /* --- push + display + undo/redo --------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the edit is queued");
    check(same_plan(edits.apply(plan), plan),
          label + ": the display is unchanged (the shell shows the sort for the session)");
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
    check_size(saved.sorts_written, 1, label + ": the cascade wrote the sort");
    check(saved.api.find(" save_view_sort") != std::string::npos,
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

    /* Every stream, under the null-save discipline.  Name ascending leaves the rows
     * where they are: its edit list has no effect to declare, so the declared set
     * is the view stream and the marker (the order of the array matters: the
     * leading entries are the declared ones). */
    const std::array<const char*, 3> declared{{kViewVar2, kMarker, kEdlVar2}};
    const std::size_t declared_count = p.order_changes ? 3 : 2;
    const Context context;
    save_pair::compare_all(context, label, control, null_save, project, output, nullptr, 0,
                           declared.data(), declared_count);

    /* Every stream OUTSIDE the owned ones is byte-identical to the control. */
    for (const std::string& path : control.stream_paths()) {
        if (path == kViewVar2 || path == kEdlVar2 || path == kMarker) {
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

void expect_refused(const app::PlanInput& plan, const std::string& source,
                    const std::vector<app::SortKeyRequest>& keys, bool outline, bool renumber,
                    const char* needle, const std::string& what) {
    app::StructuralEdit edit;
    std::string error;
    const bool built = app::make_sort_edit(plan, source, keys, outline, renumber, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what.c_str());
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
    check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0 && edit.field_id == 0 &&
              edit.sort_order.empty(),
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

app::StructuralEdit hand_edit(const Pair& p) {
    app::StructuralEdit hand;
    hand.kind = app::StructuralKind::sort_view;
    hand.field_id = p.field_id;
    hand.ascending = p.ascending;
    hand.sort_base_order = kBase;
    hand.sort_order = p.sorted;
    return hand;
}

void check_refusals() {
    const std::vector<Pair> all = pairs();
    const Pair& named = all[0];
    const std::filesystem::path control_path = data_path(control_of(named));
    const std::filesystem::path sorted_path = data_path(edit_of(named));
    app::PlanInput plan;
    try {
        plan = load_plan(control_path);
    } catch (const std::exception&) {
        check(false, "the refusal fixtures load");
        return;
    }
    const std::string source = control_path.string();
    const auto name_desc = one_key(app::SortKeyKind::name, false);

    /* The sort shape (brief rule 1: one pair per shape). */
    expect_refused(plan, source, {}, true, false, "0 keys", "no key");
    {
        std::vector<app::SortKeyRequest> two = name_desc;
        app::SortKeyRequest second;
        second.kind = app::SortKeyKind::duration;
        two.push_back(second);
        expect_refused(plan, source, two, true, false, "exactly one key",
                       "two keys (the two-key pair is evidence only)");
    }
    expect_refused(plan, source, one_key(app::SortKeyKind::id, true), true, false,
                   "Sort by ID", "an ID key");
    expect_refused(plan, source, one_key(app::SortKeyKind::start, true), true, false,
                   "Sort by Start", "a Start key");
    expect_refused(plan, source, one_key(app::SortKeyKind::finish, false), true, false,
                   "Sort by Finish", "a Finish key");
    expect_refused(plan, source, one_key(app::SortKeyKind::other, true), true, false,
                   "Sort by this key", "any other key");
    expect_refused(plan, source, name_desc, false, false,
                   "Keep outline structure unchecked", "Outline = False");
    expect_refused(plan, source, name_desc, true, true, "Permanently renumber",
                   "Renumber = True");

    /* The plan: the measured row count, saved rows, and the pinned comparisons. */
    {
        app::PlanInput fewer = plan;
        for (std::size_t index = 0; index < fewer.tasks.size(); ++index) {
            if (fewer.tasks[index].uid == 5) {
                fewer.tasks.erase(fewer.tasks.begin() + static_cast<std::ptrdiff_t>(index));
                break;
            }
        }
        expect_refused(fewer, source, name_desc, true, false, "project with 4 tasks",
                       "four rows");
    }
    {
        app::PlanInput pending = plan;
        task_by_uid(pending, 5)->file_row = 0;
        expect_refused(pending, source, name_desc, true, false, "while a task is pending",
                       "a pending task");
    }
    {
        app::PlanInput prefix = plan;
        const std::string d = task_by_uid(prefix, 4)->name;
        task_by_uid(prefix, 5)->name = d + "2";
        expect_refused(prefix, source, name_desc, true, false, "differ at a letter of the same case",
                       "a sibling name that extends another");
    }
    {
        app::PlanInput cased = plan;
        std::string lower = task_by_uid(cased, 4)->name;
        lower[0] = static_cast<char>(lower[0] + ('a' - 'A'));
        task_by_uid(cased, 4)->name = lower;
        expect_refused(cased, source, name_desc, true, false, "differ at a letter of the same case",
                       "sibling names that differ at a letter of another case");
    }
    {
        app::PlanInput digits = plan;
        task_by_uid(digits, 4)->name = "Task 1";
        task_by_uid(digits, 5)->name = "Task 2";
        expect_refused(digits, source, name_desc, true, false, "differ at a letter of the same case",
                       "sibling names that differ at a digit");
    }
    {
        app::PlanInput equal = plan;
        task_by_uid(equal, 4)->duration_ticks = task_by_uid(equal, 5)->duration_ticks;
        expect_refused(equal, source, one_key(app::SortKeyKind::duration, false), true, false,
                       "equal durations", "equal sibling durations");
    }

    /* W35b-1.4 (R35G2-4): Name in both directions and Duration descending are
     * measured; Duration ascending is not, and the one ascending pair moves no row. */
    {
        expect_refused(plan, source, one_key(app::SortKeyKind::duration, true), true, false,
                       "ascending Duration sorts are unmeasured", "Duration ascending");
        app::PlanInput moved = plan;
        task_by_uid(moved, 1)->name = "Task Z";
        expect_refused(moved, source, one_key(app::SortKeyKind::name, true), true, false,
                       "an ascending sort that reorders rows is unmeasured",
                       "a Name ascending sort that would move rows");

        app::StructuralEdits edits;
        app::StructuralEdit duration_up = hand_edit(named);
        duration_up.field_id = 0x0B400507u;
        duration_up.ascending = true;
        duration_up.sort_order = {3, 2, 1, 5, 4};
        check(!edits.push(duration_up, &plan), "push() refuses Duration ascending");
        app::StructuralEdit name_up = hand_edit(named);
        name_up.ascending = true;
        check(!edits.push(name_up, &plan),
              "push() refuses an ascending Name sort that carries a reordered result");
        name_up.sort_order = kBase;
        check(edits.push(name_up, &plan),
              "push() accepts Name ascending that moves no row (the measured pair)");
    }

    /* W35b-1.2 (R35G2-2): a plan that is not the file's (an unsaved rename, or an
     * outline the file does not store) gives an order the file's own rows do not,
     * and the writer's dry run refuses it when the edit is built. */
    {
        app::PlanInput renamed = plan;
        task_by_uid(renamed, 5)->name = "Task C";
        expect_refused(renamed, source, name_desc, true, false,
                       "carries unsaved edits (UniqueID 5 Name differs from the file)",
                       "a plan whose names are not the file's");
        /* W35b-1.3 (R35G2-3): the pairs measure one outline (levels 0, 1, 1, 0, 0). */
        struct Shape {
            std::array<int, 5> levels;
            const char* needle;
            const char* what;
        };
        const std::vector<Shape> shapes = {
            {{0, 1, 2, 0, 0}, "and this one has levels 1, 2, 3, 1, 1", "a three-level outline"},
            {{0, 0, 0, 0, 0}, "and this one has levels 1, 1, 1, 1, 1", "a flat outline"},
            {{0, 1, 1, 1, 0}, "and this one has levels 1, 2, 2, 2, 1",
             "a third child under the summary"},
            {{0, 1, 0, 1, 0}, "and this one has levels 1, 2, 1, 2, 1", "two summaries"},
            {{0, 2, 1, 0, 0}, "deeper than its parent allows", "a row deeper than its parent"},
        };
        for (const Shape& shape : shapes) {
            app::PlanInput reindented = plan;
            for (std::uint32_t uid = 1; uid <= 5; ++uid) {
                task_by_uid(reindented, uid)->outline_level = shape.levels[uid - 1];
            }
            expect_refused(reindented, source, name_desc, true, false, shape.needle,
                           std::string("a plan with ") + shape.what);
            expect_refused(reindented, source, one_key(app::SortKeyKind::duration, false), true,
                           false, shape.needle,
                           std::string("a Duration sort of a plan with ") + shape.what);
        }
        app::PlanInput lengthened = plan;
        task_by_uid(lengthened, 5)->duration_ticks = 100;
        task_by_uid(lengthened, 4)->duration_ticks = 200;
        task_by_uid(lengthened, 1)->duration_ticks = 300;
        expect_refused(lengthened, source, one_key(app::SortKeyKind::duration, false), true, false,
                       "carries unsaved edits (UniqueID 1 Duration differs from the file)",
                       "a plan whose durations are not the file's");

        /* W35b-1.5 (RW35-R2): a grid edit the plan carries and the file does not,
         * on any field a grid edit reaches, is refused with the actionable reason;
         * the measured plan (the file's own rows) still builds in the pair tests. */
        struct Unsaved {
            const char* needle;
            const char* what;
            std::uint32_t uid;
            int field;
        };
        const std::vector<Unsaved> unsaved = {
            {"UniqueID 2 Name differs from the file", "a renamed task", 2, 0},
            {"UniqueID 3 Duration differs from the file", "a typed Duration", 3, 1},
            {"UniqueID 4 % Complete differs from the file", "a typed % Complete", 4, 2},
            {"UniqueID 1 Summary differs from the file", "a flipped Summary", 1, 3},
            {"UniqueID 5 Milestone differs from the file", "a flipped Milestone", 5, 4},
        };
        for (const Unsaved& edit_case : unsaved) {
            app::PlanInput edited = plan;
            app::TaskRow* row = task_by_uid(edited, edit_case.uid);
            switch (edit_case.field) {
                case 0:
                    row->name += "x";
                    break;
                case 1:
                    row->duration_ticks += 480;
                    break;
                case 2:
                    row->percent_complete = 50;
                    break;
                case 3:
                    row->summary = !row->summary;
                    break;
                default:
                    row->milestone = !row->milestone;
                    break;
            }
            expect_refused(edited, source, name_desc, true, false, edit_case.needle,
                           std::string("a sort with ") + edit_case.what);
            expect_refused(edited, source, name_desc, true, false,
                           "save or undo the grid edits first",
                           std::string("a sort with ") + edit_case.what + " asks to save or undo");
        }

        /* The same hand-built edit passes push() (the plan gives it) and the save
         * refuses it from the file before anything is written. */
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::sort_view;
        hand.field_id = named.field_id;
        hand.ascending = named.ascending;
        hand.sort_base_order = kBase;
        hand.sort_order = {4, 5, 1, 3, 2};
        app::StructuralEdits edits;
        check(edits.push(std::move(hand), &renamed),
              "push() accepts the order the renamed plan gives");
        const std::filesystem::path out = out_path("app-sort-plan-not-file.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, source, out.string(), false);
        check(!saved.ok, "the save refuses an order the file's own names do not give");
        check(saved.error.find("the sorted order does not match the order the file's own "
                               "outline") != std::string::npos,
              "the save's refusal names the file's order (got \"" + saved.error + "\")");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* The file decides the rest (the writer's dry run). */
    expect_refused(plan, sorted_path.string(), name_desc, true, false, "already carries a sort",
                   "a file that already carries a sort");
    expect_refused(plan, data_path("wave31-resource-sheet.mpp").string(), name_desc, true, false,
                   "carries unsaved edits (UniqueID 1 Summary differs from the file)",
                   "a file whose tasks are not the plan's (W35b-1.5: the rows are compared "
                   "before the applied view; the view pin is the writer test's)");
    expect_refused(plan, (std::filesystem::path(APP_TEST_OUT_DIR) / "no-such-file.mpp").string(),
                   name_desc, true, false, "could not be read", "a missing project file");

    /* push(): the plan-side backstop for a hand-built queue. */
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(named), nullptr), "push() refuses a sort with no plan");
        app::StructuralEdit wrong_order = hand_edit(named);
        wrong_order.sort_order = {1, 2, 3, 4, 5};
        check(!edits.push(wrong_order, &plan), "push() refuses an order the plan does not give");
        app::StructuralEdit wrong_base = hand_edit(named);
        wrong_base.sort_base_order = {5, 4, 3, 2, 1};
        check(!edits.push(wrong_base, &plan), "push() refuses a base order that is not the file's");
        app::StructuralEdit wrong_field = hand_edit(named);
        wrong_field.field_id = 0x0B40000Fu;
        check(!edits.push(wrong_field, &plan), "push() refuses a key other than Name or Duration");
        app::StructuralEdit with_row = hand_edit(named);
        with_row.uid = 3;
        check(!edits.push(with_row, &plan), "push() refuses a sort that names a row");
        check(edits.empty(), "no refused sort left anything in the queue");
        check(edits.push(hand_edit(named), &plan), "push() accepts the hand-built measured sort");
    }

    /* Exclusivity, in both directions. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(named), &plan), "push() refuses a sort over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(named), &plan), "a sort queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan), "push() refuses another edit over a sort");
        check(!edits.push(hand_edit(named), &plan), "push() refuses a second sort");
        check_size(edits.size(), 1, "only the sort stays queued");
    }

    /* The cascade refuses a start-date edit beside a sort before it writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_sort_edit(plan, source, name_desc, true, false, edit, error),
              "the exclusivity fixture builds its sort");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the sort");
        const std::filesystem::path out = out_path("app-sort-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses a sort with a start-date edit");
        check(saved.error.find("a Sort edit cannot be combined with a start-date") !=
                  std::string::npos,
              "the save's refusal names the start-date edit (got \"" + saved.error + "\")");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* A pending table-format or formula edit reaches the same save through the
     * document-edit hook; the cascade refuses it before the hook or any write. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_sort_edit(plan, source, name_desc, true, false, edit, error),
              "the document-edit fixture builds its sort");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the document-edit fixture queues its sort");
        const std::filesystem::path out = out_path("app-sort-document-edit-refused.mpp");
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
            const std::string what = std::string("its sort with a pending ") +
                                     (as_formula ? "formula" : "table format") + " edit";
            const app::StructuralSaveResult saved =
                app::save_structural_edits(edits, {}, source, out.string(), false, request);
            check(!saved.ok, "the save refuses " + what);
            check(saved.error.find("a Sort edit cannot be combined with a table format") !=
                      std::string::npos,
                  "the save's refusal names the combination for " + what + " (got \"" +
                      saved.error + "\")");
            check(hook_calls == 0, "the hook never ran for " + what);
            check(!std::filesystem::exists(out), "the refused save writes no target for " + what);
            check(edits.applied().size() == queued, "the queue is unchanged after " + what);
        }
    }

    /* A file that changed after the edit was queued: the same edit saved against
     * Project's already-sorted file is refused before anything is written. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_sort_edit(plan, source, name_desc, true, false, edit, error),
              "the changed-file fixture builds its sort");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the changed-file fixture queues it");
        const std::filesystem::path out = out_path("app-sort-changed-file.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, sorted_path.string(), out.string(), false);
        check(!saved.ok, "the save fails on a view that already carries the sort");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
        check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
              "the refused save leaves no intermediate file");
    }
}

}  // namespace

int main() {
    for (const Pair& p : pairs()) {
        check_pair(p);
    }
    check_refusals();
    std::printf("app_edits_sort_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_sort_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
