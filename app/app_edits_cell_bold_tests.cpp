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
// with a committed null save of the control (the null-save mask source), and
// (W36-1.1, Bold off through the same builder):
//
//   wave35-g3-font-bold-off-control.mpp    -> wave35-g3-font-bold-off.mpp
//       the same cell with Bold turned off; Project rewrites the item in place
//
// and (W36-1.2, held to the lift rule by W36-1b.1: Bold on the three measured cells
// (row 1 Name, row 2 Name, row 1 Start) and no other; each is a measured shape, not
// a held-out prediction, because trace-font-item-r6.md derives the row and field
// rule from these same runs):
//
//   wave35-g3-font-bold-row2-control.mpp   -> wave35-g3-font-bold-row2.mpp
//       the Task Name cell of row 2
//   wave35-g3-font-bold-start-control.mpp  -> wave35-g3-font-bold-start.mpp
//       the Start cell of row 1
//   wave35-g3-font-bold-range-control.mpp  -> wave35-g3-font-bold-range.mpp
//       the Task Name cell of row 2 again (a one-row Bold, mislabelled "range")
//
// The held-out pair, wave36-r7-font-bold-session2 (wave30-textstyle's edit made in a
// second Project session on a reopened control), is checked in
// tests/roundtrip_viewstream.cpp (test_bold_cell_pairs), where its two unmatched
// streams, Props14 and 214/CEdl/Var2Data, are named expected failures.  It is not a
// pair here: this table has no named gaps.
//
// The font word's top byte (item payload +43) is written by no instruction in the
// trace and differs by session (0x90, 0x6b, 0x00); the port pins 0x90.  Each
// comparison masks that one byte by its exact field (the 0x2240000c item at
// Var2Data 85,272, payload +43) in Project's save, never by stream name.
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
/* W36-1.1: Bold off on the same cell; the control already carries the Bold item. */
const char* const kOffControl = "wave35-g3-font-bold-off-control.mpp";
const char* const kOffNull = "wave35-g3-font-bold-off-control-null.mpp";
const char* const kOffProject = "wave35-g3-font-bold-off.mpp";
/* Bold off owns two streams: the item's bytes in Var2Data and the Props14 marker. */
const std::array<const char*, 2> kOffDeclared{{kViewVar2, kMarker}};
/* The pair's effect bytes: two in Var2Data (flags byte, font word byte), one in Props14. */
constexpr std::size_t kOffPairEffects = 3;

/* The total of the pair's effect bytes (tests/roundtrip_viewstream.cpp:
 * Var2Data 6542, VarMeta 7, 214/Props 1, Props14 1). */
constexpr std::size_t kPairEffects = 6607;

constexpr std::uint32_t kTaskUid = app::kBoldMeasuredTaskUid;
constexpr std::uint32_t kNameField = app::kBoldMeasuredFieldId;
constexpr std::uint32_t kStartField = 0x0B400505u;
constexpr std::uint32_t kMeasuredFontWord = 0x90000001u;

/* One committed Bold pair: the cell it formats and the font word's top byte in
 * Project's save (the session's leftover, payload +43 of the item). */
struct BoldPair {
    const char* label;
    const char* control;
    const char* null_save;
    const char* project;
    std::uint32_t uid;
    std::uint32_t field;
    std::uint32_t project_top;
    std::size_t effects;
    bool italic = false;  /* W36-1.3: the pair is Italic (flags 0x02000003, font word 0x04) */
};

/* The item the view's main block gains: Var2Data 42,576 + 4 + 42,692 = 85,272,
 * and its font word's top byte at payload +43 (header 12 + 43). */
constexpr std::size_t kItemAt = 42576 + 4 + 42692;
constexpr std::size_t kItemFontTopAt = kItemAt + 12 + 43;

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
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
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

void check_pair(const BoldPair& spec) {
    const std::string label = spec.label;
    const std::uint32_t task_uid = spec.uid;
    const std::uint32_t field = spec.field;
    const std::filesystem::path control_path = data_path(spec.control);
    const std::filesystem::path result_path = data_path(spec.project);
    const std::filesystem::path null_path = data_path(spec.null_save);
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);
    check(null_path.filename().string() == save_pair::null_fixture_name(spec.control),
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
    const CellEntry before = read_cell(control_path.string(), task_uid, field);
    check(!before.found, label + ": the control has no cell format for the cell");
    const CellEntry project_cell = read_cell(result_path.string(), task_uid, field);
    check(project_cell.found, label + ": Project's save has the cell format entry");
    /* The font word is the attribute's bit (Bold 0x01, Italic 0x04) under the
     * session's leftover top byte. */
    const std::uint32_t attribute_font_word = spec.italic ? 0x00000004u : kMeasuredFontWord;
    const std::uint32_t attribute_flags = spec.italic ? 0x02000003u : 0x01000003u;
    check_size(project_cell.font_word & 0x00FFFFFFu, attribute_font_word & 0x00FFFFFFu,
               label + ": Project's font word is the attribute's (top byte aside)");
    check_size(project_cell.flags, attribute_flags,
               label + ": Project's flags word is the attribute's mask");
    check_size(project_cell.font_word >> 24, spec.project_top,
               label + ": Project's font word top byte is the session's leftover");

    /* --- make_cell_bold_edit ---------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built =
        spec.italic ? app::make_cell_italic_edit(plan, control_path.string(), task_uid, field,
                                                 edit, edit_error)
                    : app::make_cell_bold_edit(plan, control_path.string(), task_uid, field,
                                               edit, edit_error);
    check(built, label + (spec.italic ? ": make_cell_italic_edit accepts the measured cell"
                                      : ": make_cell_bold_edit accepts the measured cell"));
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == (spec.italic ? app::StructuralKind::italic_cell
                                    : app::StructuralKind::bold_cell),
          label + ": the built edit has the kind");
    check_size(edit.uid, task_uid, label + ": the edit names the row");
    check_size(edit.field_id, field, label + ": the edit names the cell's field");
    check(edit.cell_bold_on, label + ": an unformatted cell builds the attribute ON");

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
    const std::filesystem::path out = out_path(std::string("app-") + spec.label + ".mpp");
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

    /* The font word's top byte, masked by its exact field: the 0x2240000c item at
     * Var2Data 85,272 (length 44, row and field key checked), payload +43.  Both
     * saves are asserted first (the port pins 0x90, Project holds the session's
     * leftover) and only Project's byte is then set to the port's. */
    Bytes project_var2 = stream_or_empty(project, kViewVar2);
    const Bytes output_var2 = stream_or_empty(output, kViewVar2);
    check_size(project_var2.size(), output_var2.size(),
               label + ": the port's Var2Data is Project's size");
    if (project_var2.size() > kItemFontTopAt && output_var2.size() > kItemFontTopAt) {
        const Bytes* const both[2] = {&project_var2, &output_var2};
        for (const Bytes* var2 : both) {
            check_size(u32_at(*var2, kItemAt), 44, label + ": the item's length is 44");
            check_size(u32_at(*var2, kItemAt + 4), 0x2240000Cu,
                       label + ": the item's id is 0x2240000c");
            check_size(u32_at(*var2, kItemAt + 12), task_uid, label + ": payload +0 is the row");
            check_size(u32_at(*var2, kItemAt + 16), field, label + ": payload +4 is the field");
        }
        check_size(output_var2[kItemFontTopAt], 0x90, label + ": the port pins the top byte 0x90");
        check_size(project_var2[kItemFontTopAt], spec.project_top,
                   label + ": Project's top byte is the session's leftover");
        project_var2[kItemFontTopAt] = output_var2[kItemFontTopAt];
        project.replace_stream(kViewVar2, project_var2);
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
    check_size(effects, spec.effects, label + ": the pair's declared effect bytes");

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

    /* The app's view reader reads back what Project saved, field by field (the
     * font word's top byte aside, which is the masked field). */
    const CellEntry after = read_cell(out.string(), task_uid, field);
    check(after.found, label + ": the port's save carries the cell format entry");
    check_size(after.flags, project_cell.flags, label + ": entry flags equal Project's");
    check_size(after.font_word & 0x00FFFFFFu, project_cell.font_word & 0x00FFFFFFu,
               label + ": entry font word equals Project's (top byte aside)");
    check_size(after.font_word & 0x00FFFFFFu, attribute_font_word & 0x00FFFFFFu,
               label + ": the port's entry carries the attribute's font bit");
    check_size(after.font_word >> 24, 0x90, label + ": the port's entry top byte is the pinned 0x90");
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

/* --- Bold off (W36-1.1) ----------------------------------------------------- */

void check_off_pair() {
    const std::string label = "cell-bold-off";
    const std::filesystem::path control_path = data_path(kOffControl);
    const std::filesystem::path result_path = data_path(kOffProject);
    const std::filesystem::path null_path = data_path(kOffNull);
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);
    check(null_path.filename().string() == save_pair::null_fixture_name(kOffControl),
          label + ": the null save is the control's committed null fixture");

    app::PlanInput plan;
    try {
        plan = load_plan(control_path);
    } catch (const std::exception& error) {
        check(false, label + ": the control loads through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }

    /* The control carries the Bold item; Project's save carries it turned off. */
    const CellEntry before = read_cell(control_path.string(), kTaskUid, kNameField);
    check(before.found, label + ": the control carries the cell format entry");
    check_size(before.flags, 0x01000003u, label + ": the control's flags are Bold's");
    check_size(before.font_word, kMeasuredFontWord, label + ": the control's font word is Bold's");
    const CellEntry project_cell = read_cell(result_path.string(), kTaskUid, kNameField);
    check(project_cell.found, label + ": Project's save keeps the cell format entry");
    check_size(project_cell.flags, 0x00000003u, label + ": Project's flags are 0x00000003");
    check_size(project_cell.font_word, 0x90000000u, label + ": Project's font word is 0x90000000");

    /* The same builder the Bold button calls decides from the file. */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_cell_bold_edit(plan, control_path.string(), kTaskUid,
                                                kNameField, edit, edit_error);
    check(built, label + ": make_cell_bold_edit accepts the Bold item's cell");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::bold_cell, label + ": the built edit has the kind");
    check_size(edit.uid, kTaskUid, label + ": the edit names the row");
    check_size(edit.field_id, kNameField, label + ": the edit names the cell's field");
    check(!edit.cell_bold_on, label + ": a bold cell builds Bold OFF");

    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the edit is queued");
    check(same_plan(edits.apply(plan), plan), label + ": the display is unchanged");
    check(edits.undo(), label + ": the edit undoes");
    check(edits.empty(), label + ": undo empties the queue");
    check(edits.redo(), label + ": the edit redoes");
    check_size(edits.size(), 1, label + ": redo restores the one edit");
    check(!edits.applied().front().cell_bold_on, label + ": redo keeps the edit OFF");

    const std::filesystem::path out = out_path("app-cell-bold-off.mpp");
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
    bool notes_off = false;
    for (const std::string& note : saved.notes) {
        notes_off = notes_off || note.find("turned off") != std::string::npos;
    }
    check(notes_off, label + ": the save's note says the cell was turned off");

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

    /* Every stream, under the null-save discipline, the two owned streams declared. */
    const Context context;
    save_pair::compare_all(context, label, control, null_save, project, output, nullptr, 0,
                           kOffDeclared.data(), kOffDeclared.size());
    const std::vector<save_pair::TailNormalisation> no_normalisation;
    std::size_t effects = 0;
    for (const char* path : kOffDeclared) {
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
    check_size(effects, kOffPairEffects, label + ": the pair's declared effect bytes");

    for (const std::string& path : control.stream_paths()) {
        bool owned = false;
        for (const char* declared : kOffDeclared) {
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
    check(after.found, label + ": the port's save keeps the cell format entry");
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
    expect_refused(plan, source, kTaskUid, 0, "Task Name and Start columns", "field id 0");
    expect_refused(plan, source, kTaskUid, 0x0B40000Fu, "Task Name and Start columns",
                   "a column other than Task Name and Start");
    expect_refused(plan, source, kTaskUid, 0x0B400506u, "Task Name and Start columns",
                   "the Finish column (unmeasured)");
    /* The pair's Italic, Underline, strikethrough and colours are not even a
     * parameter of the builder: Font Bold is the only attribute it builds. */
    std::uint32_t other_task = 0;
    std::uint32_t third_task = 0;
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid != kTaskUid && row.uid != 0 && row.file_row != 0 && other_task == 0) {
            other_task = row.uid;
        } else if (row.uid != kTaskUid && row.uid != other_task && row.uid != 0 &&
                   row.file_row != 0 && third_task == 0) {
            third_task = row.uid;
        }
    }
    check(other_task != 0, "the control has a second saved task for the row refusal");
    check(third_task != 0, "the control has a third saved task for the row refusal");
    /* W36-1b.1 (R36G1-1): Bold ON is built for (row 1, Name), (row 2, Name) and
     * (row 1, Start) only.  A later row, and the joint cell (row 2, Start), have no
     * pair and are refused with the three cells named. */
    check(other_task == 2, "the second saved task of the control is row uid 2");
    expect_refused(plan, source, third_task, kNameField,
                   "the Task Name cell of rows 1 and 2 and the Start cell of row 1",
                   "Bold on a row beyond the measured ones (Task Name)");
    expect_refused(plan, source, third_task, kStartField,
                   "the Task Name cell of rows 1 and 2 and the Start cell of row 1",
                   "Bold on a row beyond the measured ones (Start)");
    expect_refused(plan, source, other_task, kStartField,
                   "the Task Name cell of rows 1 and 2 and the Start cell of row 1",
                   "Bold on the joint cell (row 2, Start)");
    /* W36-1.2: no pair separates a row's ID from its UniqueID (equal in every
     * committed pair), so a task whose two differ is refused rather than guessed.
     * W36-1b.1: and a cell that no pair measured is refused before that. */
    {
        app::PlanInput renumbered = plan;
        for (app::TaskRow& row : renumbered.tasks) {
            if (row.uid == other_task) {
                row.id = row.uid + 40;
            }
        }
        expect_refused(renumbered, source, other_task, kNameField, "ID differs from its UniqueID",
                       "a task whose ID differs from its UniqueID");
    }
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
    /* W36-1.1: a view whose one item is the Bold item of the cell is Bold OFF
     * (check_off_pair), so the "already formatted" refusals use a view whose item
     * is neither: the cell already turned off, and another row's Bold item. */
    expect_refused(formatted_plan, data_path(kOffProject).string(), kTaskUid, kNameField,
                   "the view main block already carries a cell format item",
                   "a view that already carries a cell format item (Bold already off)");
    expect_refused(formatted_plan, data_path("wave35-g3-font-bold-row2.mpp").string(), kTaskUid,
                   kNameField, "the view main block already carries a cell format item",
                   "a view whose cell format item is another row's");
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
              "push() refuses a column other than Task Name and Start");
        {
            app::PlanInput renumbered = plan;
            for (app::TaskRow& row : renumbered.tasks) {
                if (row.uid == other_task) {
                    row.id = row.uid + 40;
                }
            }
            check(!edits.push(hand_edit(other_task, kNameField), &renumbered),
                  "push() refuses a task whose ID differs from its UniqueID");
        }
        check(!edits.push(hand_edit(other_task, kStartField), &plan),
              "push() refuses the joint cell (row 2, Start), which no pair measured");
        check(!edits.push(hand_edit(third_task, kNameField), &plan),
              "push() refuses a row beyond the measured ones");
        check(edits.empty(), "no refused bold left anything in the queue");
        check(edits.push(hand_edit(kTaskUid, kStartField), &plan),
              "push() accepts the Start cell of row 1 (W36-1.2)");
        check_size(edits.size(), 1, "the accepted Start bold is queued");
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
        rename.name = "R2b";
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

    /* W34b-1.3 (RC-R3, R34G4-8): a pending table-format or custom-field formula
     * edit reaches the same save through the document-edit hook, so the cascade
     * refuses the combination before the hook or any write; the queue is kept. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_cell_bold_edit(plan, source, kTaskUid, kNameField, edit, error),
              "the document-edit fixture builds its bold");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the document-edit fixture queues its bold");
        const std::filesystem::path out = out_path("app-cell-bold-document-edit-refused.mpp");
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
            const std::string what = std::string("its bold with a pending ") +
                                     (as_formula ? "formula" : "table format") + " edit";
            const app::StructuralSaveResult saved = app::save_structural_edits(
                edits, {}, source, out.string(), false, request);
            check(!saved.ok, "the save refuses " + what);
            check(saved.error.find("a Font Bold edit cannot be combined with a table format") != std::string::npos,
                  "the save's refusal names the combination for " + what + " (got \"" +
                      saved.error + "\")");
            check(hook_calls == 0, "the hook never ran for " + what);
            check(!std::filesystem::exists(out), "the refused save writes no target for " + what);
            check(edits.applied().size() == queued, "the queue is unchanged after " + what);
        }
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

    /* W36-1.3: Italic has one pair (row 1, Task Name, an unformatted view), so
     * every other shape is refused: another row, another column, an unsaved task,
     * a view that already carries a format item (Italic off and a second change
     * are unmeasured), and a hand-built edit for another cell. */
    {
        auto italic_refused = [&](const app::PlanInput& from, const std::string& file,
                                  std::uint32_t task, std::uint32_t field, const char* needle,
                                  const std::string& what) {
            app::StructuralEdit edit;
            std::string error;
            const bool built = app::make_cell_italic_edit(from, file, task, field, edit, error);
            check(!built, what);
            if (built) {
                return;
            }
            check(error.find(needle) != std::string::npos,
                  what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
            check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0 &&
                      edit.field_id == 0,
                  what + ": the refused edit is left default-constructed");
        };
        italic_refused(plan, source, 4242, kNameField, "not on a saved task",
                       "Italic on an unknown task");
        italic_refused(plan, source, kTaskUid, kStartField, "outside the Task Name column",
                       "Italic on the Start cell");
        italic_refused(plan, source, other_task, kNameField, "UniqueID 1",
                       "Italic on another row's Task Name cell");
        italic_refused(formatted_plan, formatted_path.string(), kTaskUid, kNameField,
                       "turning Italic off is not measured",
                       "Italic on a view that already carries a format item");
        /* Bold on an Italic file is a different attribute on a formatted block: the
         * Bold toggle must not read the Italic item as its own Bold item. */
        {
            const std::filesystem::path italic_file =
                data_path("wave35-g3-font-italic.mpp");
            fixture::require_committed(italic_file);
            expect_refused(load_plan(italic_file), italic_file.string(), kTaskUid, kNameField,
                           "already carries a cell format item",
                           "Bold on the cell Project italicised");
        }
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::italic_cell;
        hand.uid = other_task;
        hand.field_id = kNameField;
        app::StructuralEdits edits;
        check(!edits.push(hand, &plan), "push() refuses an Italic edit for another row");
        hand.uid = kTaskUid;
        hand.field_id = kStartField;
        check(!edits.push(hand, &plan), "push() refuses an Italic edit for the Start cell");
        hand.field_id = kNameField;
        check(edits.push(hand, &plan), "push() accepts the measured Italic cell");
    }
}

}  // namespace

int main() {
    /* The effect counts of the three new pairs are the measured ones (printed as
     * "declared effect bytes" by each run); textstyle's is the wave 34 figure. */
    const std::array<BoldPair, 5> pairs{{
        {"cell-bold", kControl, kNull, kProject, kTaskUid, kNameField, 0x90, kPairEffects},
        {"cell-bold-row2", "wave35-g3-font-bold-row2-control.mpp",
         "wave35-g3-font-bold-row2-control-null.mpp", "wave35-g3-font-bold-row2.mpp", 2,
         kNameField, 0x6b, 6607},
        {"cell-bold-start", "wave35-g3-font-bold-start-control.mpp",
         "wave35-g3-font-bold-start-control-null.mpp", "wave35-g3-font-bold-start.mpp", 1,
         kStartField, 0x00, 6608},
        {"cell-bold-range", "wave35-g3-font-bold-range-control.mpp",
         "wave35-g3-font-bold-range-control-null.mpp", "wave35-g3-font-bold-range.mpp", 2,
         kNameField, 0x00, 6607},
        /* W36-1.3: the only Italic pair (row 1, Task Name, top byte 0x6b); the effect
         * count is the measured one. */
        {"cell-italic", "wave35-g3-font-italic-control.mpp",
         "wave35-g3-font-italic-control-null.mpp", "wave35-g3-font-italic.mpp", 1,
         kNameField, 0x6b, 6607, true}}};
    for (const BoldPair& pair : pairs) {
        check_pair(pair);
    }
    check_off_pair();
    check_refusals();
    // W35-4.1: the range refusal both shells print names the cause and the pair.
    {
        const std::string text(app::bold_range_refusal());
        check(text.find("more than one cell") != std::string::npos &&
                  text.find("single cell") != std::string::npos &&
                  text.find("is not implemented in this port") != std::string::npos,
              "the shared Font Bold range refusal names the cause and the measured pair");
        const std::string italic_text(app::italic_range_refusal());
        check(italic_text.find("Font Italic on more than one cell") != std::string::npos &&
                  italic_text.find("is not implemented in this port") != std::string::npos,
              "the shared Font Italic range refusal names the cause");
    }
    std::printf("app_edits_cell_bold_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_cell_bold_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
