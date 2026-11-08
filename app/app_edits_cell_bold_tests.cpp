// app/app_edits_cell_bold_tests.cpp -- Font Bold on one cell through the app's
// edit model and save cascade against Project's own save pair (wave 34, W34-4.5).
//
// The committed pair (spec/writers/view-stream-214.md, W34-2.1):
//
//   wave30-textstyle-control.mpp   -> wave30-textstyle.mpp
//       the Task Name cell of the task with UniqueID 1 made Bold; Project
//       appends one 0x2240000c cell-format item to the applied view's main
//       block and re-points the later view blocks
//
// with a committed null save of the control (the null-save mask source).
//
// This test:
//
//   1. loads the control through the app's reader adapter and builds the edit
//      with make_cell_bold_edit();
//   2. queues it, proves the DISPLAY is unchanged (Bold lives in the view
//      stream, not in the plan) and that undo/redo work;
//   3. saves it with the real cascade and compares every stream of the port's
//      save against Project's under the null-save discipline (save_pair::
//      compare_all) with the four owned streams DECLARED changed so a mask that
//      swallowed the change fails `effects_ok`; the effect bytes are counted;
//   4. reopens the port's save through the app's view reader and proves the
//      cell format entry is the one Project saved, field by field.
//
// It also checks every refusal the edit model owes the user (brief rule 2: one
// pair is one data point, so only the measured cell is enabled): another row,
// another column, an unknown or unsaved task, a view that already carries a
// cell format, a damaged view stream, a missing file, push()'s plan-side
// backstop, the edit's exclusivity (in the queue and in the cascade) and a
// save-time refusal on a file that changed after the edit was queued.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w34-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w34-app --config Release --target app_edits_cell_bold_tests
//   ctest --test-dir build\w34-app -C Release -R "^app_edits_cell_bold$" --output-on-failure

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
// core/mppwrite/outline.h pins the core/mppwrite/table.h header (a bare
// "table.h" would find core/view/table.h; the app include path lists
// core/view first), the same rule as app_edits_assignment_tests.cpp.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "save_pair_compare.h"
#include "save_pair_noise.h"
#include "view_style.h"

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

const char* const kViewMeta = "214/CV_iew/VarMeta";
const char* const kViewVar2 = "214/CV_iew/Var2Data";
const char* const kViewProps = "214/Props";
/* The applied view's main block in the control: 42,576 + its 4-byte length word. */
constexpr std::size_t kViewMainBlockPayloadAt = 42576 + 4;
const char* const kMarker = "Props14";

/* The four streams the measured edit owns. */
const std::array<const char*, 4> kDeclared{{kViewMeta, kViewVar2, kViewProps, kMarker}};

const char* const kControl = "wave30-textstyle-control.mpp";
const char* const kNull = "wave30-textstyle-control-null.mpp";
const char* const kProject = "wave30-textstyle.mpp";

/* The total of the pair's effect bytes (tests/roundtrip_viewstream.cpp:
 * Var2Data 6542, VarMeta 7, 214/Props 1, Props14 1). */
constexpr std::size_t kPairEffects = 6607;

constexpr std::uint32_t kTaskUid = app::kBoldMeasuredTaskUid;
constexpr std::uint32_t kNameField = app::kBoldMeasuredFieldId;
constexpr std::uint32_t kMeasuredFontWord = 0x90000001u;

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

/* The cell format entry the view stores for (uid, field), read through the app's
 * view reader; nullptr-equivalent is `found == false`. */
struct CellEntry {
    bool found = false;
    std::uint32_t flags = 0;
    std::uint32_t font_word = 0;
    std::uint32_t pattern = 0;
    bool has_background = false;
    std::size_t entries = 0;
};

CellEntry read_cell(const std::string& path, std::uint32_t uid, std::uint32_t field) {
    CellEntry result;
    msproject::viewstate::GanttStyle style;
    msproject::viewstate::CellFormatTable formats;
    std::string reason;
    if (!app::read_gantt_style(path, style, &reason, &formats)) {
        std::printf("  read_gantt_style(%s): %s\n", path.c_str(), reason.c_str());
        return result;
    }
    result.entries = formats.entries.size();
    for (const msproject::viewstate::CellFormat& entry : formats.entries) {
        if (entry.row_uid == uid && entry.field_id == field) {
            result.found = true;
            result.flags = entry.flags;
            result.font_word = entry.font_word;
            result.pattern = entry.background_pattern;
            result.has_background = entry.has_background;
        }
    }
    return result;
}

/* --- the measured pair ----------------------------------------------------- */

void check_pair() {
    const std::string label = "cell-bold";
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

    /* The control carries no format for the cell; Project's save carries one. */
    const CellEntry before = read_cell(control_path.string(), kTaskUid, kNameField);
    check(!before.found, label + ": the control has no cell format for the cell");
    const CellEntry project_cell = read_cell(result_path.string(), kTaskUid, kNameField);
    check(project_cell.found, label + ": Project's save has the cell format entry");
    check_size(project_cell.font_word, kMeasuredFontWord,
               label + ": Project's font word is the measured literal");

    /* --- make_cell_bold_edit ---------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_cell_bold_edit(plan, control_path.string(), kTaskUid,
                                                kNameField, edit, edit_error);
    check(built, label + ": make_cell_bold_edit accepts the measured cell");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::bold_cell, label + ": the built edit has the kind");
    check_size(edit.uid, kTaskUid, label + ": the edit names the row");
    check_size(edit.field_id, kNameField, label + ": the edit names the cell's field");

    /* --- push + display + undo/redo --------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the edit is queued");
    check(same_plan(edits.apply(plan), plan),
          label + ": the display is unchanged (Bold lives in the view stream)");
    check(edits.undo(), label + ": the edit undoes");
    check(edits.empty(), label + ": undo empties the queue");
    check(edits.redo(), label + ": the edit redoes");
    check_size(edits.size(), 1, label + ": redo restores the one edit");

    /* --- save through the real cascade ------------------------------------ */
    const std::filesystem::path out = out_path("app-cell-bold.mpp");
    std::error_code error;
    std::filesystem::remove(out, error);
    const app::StructuralSaveResult saved =
        app::save_structural_edits(edits, {}, control_path.string(), out.string(), false);
    check(saved.ok, label + ": the save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.cell_styles_written, 1, label + ": the cascade wrote the cell format");
    check(saved.api.find(" save_view_cell_style") != std::string::npos,
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
    save_pair::compare_all(context, label, control, null_save, project, output, nullptr, 0,
                           kDeclared.data(), kDeclared.size());

    const std::vector<save_pair::TailNormalisation> no_normalisation;
    std::size_t effects = 0;
    for (const char* path : kDeclared) {
        const save_pair::Kind kind = save_pair::kind_for_path(path);
        const save_pair::Verdict verdict = save_pair::compare(
            kind, stream_or_empty(control, path), stream_or_empty(null_save, path),
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

    /* The app's view reader reads back what Project saved, field by field. */
    const CellEntry after = read_cell(out.string(), kTaskUid, kNameField);
    check(after.found, label + ": the port's save carries the cell format entry");
    check_size(after.flags, project_cell.flags, label + ": entry flags equal Project's");
    check_size(after.font_word, project_cell.font_word, label + ": entry font word equals Project's");
    check_size(after.pattern, project_cell.pattern, label + ": entry pattern equals Project's");
    check(after.has_background == project_cell.has_background,
          label + ": entry background equals Project's");
    check_size(after.entries, project_cell.entries, label + ": the entry count equals Project's");
    try {
        const app::PlanInput reread = load_plan(out);
        check(same_plan(reread, plan), label + ": the reader reads the same plan back");
    } catch (const std::exception& read_error) {
        check(false, label + ": the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* --- refusals -------------------------------------------------------------- */

void expect_refused(const app::PlanInput& plan, const std::string& source, std::uint32_t task,
                    std::uint32_t field, const char* needle, const std::string& what) {
    app::StructuralEdit edit;
    std::string error;
    const bool built = app::make_cell_bold_edit(plan, source, task, field, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what.c_str());
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
    check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0 && edit.field_id == 0,
          what + ": the refused edit is left default-constructed");
}

app::StructuralEdit hand_edit(std::uint32_t task, std::uint32_t field) {
    app::StructuralEdit hand;
    hand.kind = app::StructuralKind::bold_cell;
    hand.uid = task;
    hand.field_id = field;
    return hand;
}

void check_refusals() {
    const std::filesystem::path control_path = data_path(kControl);
    const std::filesystem::path formatted_path = data_path(kProject);
    app::PlanInput plan;
    app::PlanInput formatted_plan;
    try {
        plan = load_plan(control_path);
        formatted_plan = load_plan(formatted_path);
    } catch (const std::exception&) {
        check(false, "the refusal fixtures load");
        return;
    }
    const std::string source = control_path.string();

    /* The key and the measured cell (brief rule 2: one pair, one cell). */
    expect_refused(plan, source, 0, kNameField, "not on a saved task", "row uid 0 (the project row)");
    expect_refused(plan, source, 4242, kNameField, "not on a saved task", "an unknown task");
    expect_refused(plan, source, kTaskUid, 0, "Task Name column", "field id 0");
    expect_refused(plan, source, kTaskUid, 0x0B40000Fu, "Task Name column",
                   "a column other than Task Name");
    /* The pair's Italic, Underline, strikethrough and colours are not even a
     * parameter of the builder: Font Bold is the only attribute it builds. */
    std::uint32_t other_task = 0;
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid != kTaskUid && row.uid != 0 && row.file_row != 0 && other_task == 0) {
            other_task = row.uid;
        }
    }
    check(other_task != 0, "the control has a second saved task for the row refusal");
    expect_refused(plan, source, other_task, kNameField, "UniqueID 1", "a task other than UniqueID 1");
    {
        app::PlanInput changed = plan;
        for (app::TaskRow& row : changed.tasks) {
            if (row.uid == kTaskUid) {
                row.file_row = 0;
            }
        }
        expect_refused(changed, source, kTaskUid, kNameField, "not on a saved task",
                       "a task that is not yet saved");
    }

    /* The file decides the rest (the writer's dry run). */
    expect_refused(formatted_plan, formatted_path.string(), kTaskUid, kNameField,
                   "the view main block already carries a cell format item",
                   "a view that already carries a cell format item");
    /* R34G4-4: the writer edits view uid 2, so the file's applied view must be
     * that view and its record the measured control's.  The two wave 31 files
     * apply the Resource Sheet (uid 4) and Resource Usage (uid 5). */
    expect_refused(plan, data_path("wave31-resource-sheet.mpp").string(), kTaskUid, kNameField,
                   "(CV_iew uid 4), not the measured view uid 2",
                   "a file whose applied view is the Resource Sheet");
    expect_refused(plan, data_path("wave31-usage.mpp").string(), kTaskUid, kNameField,
                   "(CV_iew uid 5), not the measured view uid 2",
                   "a file whose applied view is Resource Usage");
    {
        mppwrite::CfbDocument changed = mppwrite::CfbDocument::from_file(source);
        Bytes var2 = changed.read_stream(kViewVar2);
        var2[kViewMainBlockPayloadAt + 20000] ^= 0x01;
        changed.replace_stream(kViewVar2, var2);
        const std::filesystem::path changed_path = out_path("app-cell-bold-changed-view.mpp");
        changed.save(changed_path.string());
        expect_refused(plan, changed_path.string(), kTaskUid, kNameField,
                       "differs from the measured control's",
                       "a file whose applied view record is not the measured control's");
    }
    expect_refused(plan, (std::filesystem::path(APP_TEST_OUT_DIR) / "no-such-file.mpp").string(),
                   kTaskUid, kNameField, "could not be read", "a missing project file");
    {
        mppwrite::CfbDocument broken = mppwrite::CfbDocument::from_file(source);
        Bytes meta = broken.read_stream(kViewMeta);
        broken.replace_stream(kViewMeta, Bytes(meta.begin(), meta.begin() + 30));
        const std::filesystem::path broken_path = out_path("app-cell-bold-broken-meta.mpp");
        broken.save(broken_path.string());
        expect_refused(plan, broken_path.string(), kTaskUid, kNameField,
                       "214/CV_iew/VarMeta is 30 bytes, not 24 + 12*n",
                       "a file whose 214/CV_iew/VarMeta is damaged");
    }
    {
        mppwrite::CfbDocument broken = mppwrite::CfbDocument::from_file(source);
        Bytes props = broken.read_stream(kViewProps);
        broken.replace_stream(kViewProps, Bytes(props.begin(), props.begin() + 40));
        const std::filesystem::path broken_path = out_path("app-cell-bold-broken-props.mpp");
        broken.save(broken_path.string());
        expect_refused(plan, broken_path.string(), kTaskUid, kNameField,
                       "214/Props has no scalar record for tag 0x01000100",
                       "a file whose 214/Props has no view size tag");
    }

    /* push(): the plan-side backstop for a hand-built queue. */
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(kTaskUid, kNameField), nullptr), "push() refuses a bold with no plan");
        check(!edits.push(hand_edit(0, kNameField), &plan), "push() refuses row uid 0");
        check(!edits.push(hand_edit(kTaskUid, 0), &plan), "push() refuses field id 0");
        check(!edits.push(hand_edit(4242, kNameField), &plan), "push() refuses an unknown task");
        check(!edits.push(hand_edit(kTaskUid, 0x0B40000Fu), &plan),
              "push() refuses a column other than Task Name");
        check(!edits.push(hand_edit(other_task, kNameField), &plan),
              "push() refuses a task other than UniqueID 1");
        check(edits.empty(), "no refused bold left anything in the queue");
    }

    /* Exclusivity, in both directions and with the other exclusive kinds. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(kTaskUid, kNameField), &plan),
              "push() refuses a bold over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(kTaskUid, kNameField), &plan), "a bold queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan), "push() refuses another edit over a bold");
        check(!edits.push(hand_edit(kTaskUid, kNameField), &plan), "push() refuses a second bold");
        app::StructuralEdit rename;
        rename.kind = app::StructuralKind::rename_resource;
        rename.uid = 1;
        rename.name = "A longer resource name";
        check(!edits.push(std::move(rename), &plan), "push() refuses a rename over a bold");
        check_size(edits.size(), 1, "only the bold stays queued");
    }

    /* The cascade refuses a start-date edit beside a bold before it writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_cell_bold_edit(plan, source, kTaskUid, kNameField, edit, error),
              "the exclusivity fixture builds its bold");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the bold");
        const std::filesystem::path out = out_path("app-cell-bold-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses a bold with a start-date edit");
        check(saved.error.find("start-date") != std::string::npos,
              "the save's refusal names the start-date edit");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* A file that changed after the edit was queued: the same edit saved against
     * Project's already-formatted file is refused before anything is written. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_cell_bold_edit(plan, source, kTaskUid, kNameField, edit, error),
              "the changed-file fixture builds its bold");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the changed-file fixture queues it");
        const std::filesystem::path out = out_path("app-cell-bold-changed-file.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved = app::save_structural_edits(
            edits, {}, formatted_path.string(), out.string(), false);
        check(!saved.ok, "the save fails on a view that already carries the cell format");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
        check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
              "the refused save leaves no intermediate file");
    }
}

}  // namespace

int main() {
    check_pair();
    check_refusals();
    std::printf("app_edits_cell_bold_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_cell_bold_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
