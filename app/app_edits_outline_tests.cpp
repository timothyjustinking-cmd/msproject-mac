// app/app_edits_outline_tests.cpp -- Indent / Outdent through the app's edit
// model and save cascade against Project's own save pairs (wave 28, W28-2.1).
//
// For each committed pair (tests/data/wave28-outline-{indent,summary,outdent}
// and the matching -control.mpp; neutral metadata and the neutral C:/w28 drive
// path, see tests/data/README.md and core/mppwrite/outline.h) this test:
//
//   1. loads the pair's CONTROL through the app's reader adapter and builds the
//      edit with make_outline_edit() -- the app's own file-side validation and
//      calendar arithmetic -- then proves the measured words it computed:
//        indent : the new summary's +84 is 0x2ee0 for both measured indents
//                 (the calendar's working time across the moved subtree);
//        outdent: the restored marker words are 0x5dc0 / format 7 and the
//                 restored schedule is next_start(parent +104) = 0x3bf212c0,
//                 add_work(that, 0x5dc0) = 0x3bf627d8 (Project's words);
//   2. queues it with push(), checks the DISPLAY the edit model would show
//      (levels, OutlineNumber paths, summary flags, +84 durations; the +104/
//      +108 dates the sheet and Gantt draw are unchanged, as Project's save
//      leaves them), and checks undo restores the control and redo the move;
//   3. saves it with the real cascade into APP_TEST_OUT_DIR and proves the six
//      task-table streams match Project's own save:
//        FixedData  byte-identical to the pair,
//        Fixed2Meta byte-identical,
//        VarMeta    byte-identical,
//        Var2Data   byte-identical (including Project's stale bytes),
//        Fixed2Data byte-identical after canonicalising the per-save identity
//                   GUIDs (Project allocates fresh ones; every other byte --
//                   flags +42, start copy +46..49, parent links -- must match),
//        FixedMeta  the control's except the summary row's (+2 field count and
//                   +26 summary bit must equal the pair's; the opaque +8..23
//                   per-save blob is masked on that row, as W28-1.2 masks the
//                   step ids);
//   4. proves NO OTHER STREAM CHANGED: every stream outside those six is
//      byte-identical to the control.  Project's own save also rewrites
//      TBkndAssn (a task that becomes a summary loses its assignment; an
//      outdent re-creates one) and the other tables' session ids; the port
//      deliberately owns none of them (W28-0.4 precedent: Project's reopen of
//      the port's save is the acceptance test for that half).  The test
//      REPORTS Project's TBkndAssn row count rather than asserting it;
//   5. reopens the port's save through the independent core/mpp reader and
//      proves the DISPLAY edit_model shows is the file Project's reader shows:
//      outline level, OutlineNumber, summary flag, duration and the +104/+108
//      pair all equal Project's saved file, field by field.
//
// It also checks the refusals make_outline_edit()/push() owe the user: the
// first top-level row has no preceding sibling, a summary cannot be outdented,
// indenting under an existing summary is unmeasured, an unknown uid and a
// pending-deleted row are refused, and deleting either row of a queued outline
// edit displaces it (undo restores it).
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w28-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w28-app --config Release --target app_edits_outline_tests
//   ctest --test-dir build\w28-app -C Release -R "^app_edits_outline$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "layout_model.h"
// core/mppwrite/outline.h includes core/mppwrite/table.h from its own
// directory.  The app include path lists core/view first, so a bare
// "table.h" here would find core/view/table.h instead; outline.h pins the
// mppwrite header that task_row/check_table live in.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "table.h"

#if !defined(APP_TEST_DATA_DIR) || !defined(APP_TEST_OUT_DIR)
int main() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
    return 0;
}
#else

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

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

std::uint16_t get_u16(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t get_u32(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::string task_stream(const char* tail) {
    return std::string("114/TBkndTask/") + tail;
}

const std::array<std::string, 6> kOwnedTaskStreams = {
    task_stream("FixedData"), task_stream("FixedMeta"),
    task_stream("Fixed2Data"), task_stream("Fixed2Meta"),
    task_stream("VarMeta"), task_stream("Var2Data")};

bool owned_task_stream(const std::string& path) {
    return std::find(kOwnedTaskStreams.begin(), kOwnedTaskStreams.end(), path) !=
           kOwnedTaskStreams.end();
}

std::size_t uid_row(const Bytes& fixed_data, std::uint32_t uid) {
    if (fixed_data.size() < mppwrite::task_row::kHeaderSize) {
        return kNoIndex;
    }
    const std::size_t rows = (fixed_data.size() - mppwrite::task_row::kHeaderSize) /
                             mppwrite::task_row::kStride;
    for (std::size_t row = 0; row < rows; ++row) {
        if (get_u32(fixed_data, mppwrite::task_row::kHeaderSize +
                                   mppwrite::task_row::kStride * row + 4) == uid) {
            return row;
        }
    }
    return kNoIndex;
}

/* Index of the FixedMeta 47-byte entry that names this FixedData row. */
std::size_t meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < 16) {
        return kNoIndex;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(
        mppwrite::task_row::kHeaderSize + mppwrite::task_row::kStride * row);
    for (std::size_t i = 0; i < (meta.size() - 16) / 47; ++i) {
        if (get_u32(meta, 16 + i * 47 + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

/*
 * Canonicalise the session identity GUIDs of Fixed2Data: each record's +0..15
 * becomes a canonical id (row index in the low u32) and each parent link
 * (+24..39) becomes the parent's canonical id.  A link to an unknown or empty
 * id is kept verbatim (the measured controls use all-zero for no parent).
 * Project allocates fresh identities on every save; every other byte must
 * match.
 */
Bytes canonical_fixed2(const Bytes& fixed2, std::size_t rows) {
    constexpr std::size_t kRecord = 64;
    constexpr std::size_t kRowOffset = 24;
    if (fixed2.size() != kRecord * (rows + 3)) {
        return {};  /* the caller's size check reports the damage */
    }
    using Id = std::array<std::uint8_t, 16>;
    std::map<Id, Id> canonical;
    for (std::size_t row = 0; row < rows; ++row) {
        Id id{};
        const std::size_t base = kRecord * (row + 3);
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        const bool empty =
            std::all_of(id.begin(), id.end(),
                        [](std::uint8_t b) { return b == 0; });
        if (!empty) {
            Id replacement{};
            replacement[0] = static_cast<std::uint8_t>(row & 0xFFu);
            replacement[1] = static_cast<std::uint8_t>((row >> 8) & 0xFFu);
            replacement[2] = static_cast<std::uint8_t>((row >> 16) & 0xFFu);
            replacement[3] = static_cast<std::uint8_t>((row >> 24) & 0xFFu);
            canonical.emplace(id, replacement);
        }
    }
    Bytes out = fixed2;
    auto replace_id = [&out](std::size_t at, const Id& id) {
        std::copy(id.begin(), id.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(at));
    };
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t base = kRecord * (row + 3);
        Id id{};
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        const auto self = canonical.find(id);
        if (self != canonical.end()) {
            replace_id(base, self->second);
        }
        Id parent{};
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base + kRowOffset),
                  fixed2.begin() +
                      static_cast<std::ptrdiff_t>(base + kRowOffset + 16),
                  parent.begin());
        const auto found = canonical.find(parent);
        if (found != canonical.end()) {
            replace_id(base + kRowOffset, found->second);
        }
    }
    return out;
}

struct PairCase {
    const char* slug;
    const char* control;
    const char* result;
    std::uint32_t uid;        /* the moved task */
    bool indent;
    std::uint32_t anchor_uid; /* the summary row the edit touches */
    /* The measured words make_outline_edit must compute.  Indent uses
     * summary_duration; outdent uses the four restored_* values. */
    std::uint32_t summary_duration;
    std::uint32_t restored_duration;
    std::uint16_t restored_duration_format;
    std::uint32_t restored_start_raw;
    std::uint32_t restored_finish_raw;
};

const PairCase kPairs[] = {
    /* Project indented Task 2 (a leaf) under Task 1: Task 1 becomes a summary. */
    {"indent", "wave28-outline-indent-control.mpp", "wave28-outline-indent.mpp",
     2, true, 1, 0x2EE0u, 0, 0, 0, 0},
    /* Project indented Task 2 (itself a summary of Task 3) under Task 1: Task 1
     * becomes a summary of a summary. */
    {"summary", "wave28-outline-summary-control.mpp", "wave28-outline-summary.mpp",
     2, true, 1, 0x2EE0u, 0, 0, 0, 0},
    /* Project outdented Task 2 from its single-child parent Task 1: Task 1 is a
     * leaf again with the marker's stored duration and a restored schedule. */
    {"outdent", "wave28-outline-outdent-control.mpp", "wave28-outline-outdent.mpp",
     2, false, 1, 0, 0x5DC0u, 0x0007u, 0x3BF212C0u, 0x3BF627D8u},
};

const app::TaskRow* find_row(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

const mpp::Task* find_task(const mpp::Document& document, std::uint32_t uid) {
    for (const mpp::Task& task : document.tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

/* The display fields an outline edit owns: level, OutlineNumber, summary flag,
 * duration (+84).  Start/finish (+104/+108) are included because Project's save
 * leaves them where the control had them and the sheet still draws them. */
void check_row_row(const app::TaskRow& shown, const app::TaskRow& expected,
                   const char* slug, std::uint32_t uid, const char* side) {
    const std::string prefix = std::string(slug) + " " + side + " uid " +
                               std::to_string(uid);
    check(shown.outline_level == expected.outline_level,
          (prefix + " outline level").c_str());
    check_size(shown.duration_ticks, expected.duration_ticks,
               (prefix + " duration").c_str());
    check(shown.summary == expected.summary,
          (prefix + " summary flag").c_str());
    check(shown.outline_number == expected.outline_number,
          (prefix + " outline number").c_str());
    check(shown.start == expected.start, (prefix + " start").c_str());
    check(shown.finish == expected.finish, (prefix + " finish").c_str());
}

void check_display_matches(const app::PlanInput& shown,
                           const app::PlanInput& expected, const char* slug,
                           const char* side) {
    for (std::uint32_t uid = 1; uid <= 3; ++uid) {
        const app::TaskRow* a = find_row(shown, uid);
        const app::TaskRow* b = find_row(expected, uid);
        check(a != nullptr && b != nullptr,
              (std::string(slug) + " " + side + ": uid " + std::to_string(uid) +
               " is a row").c_str());
        if (a != nullptr && b != nullptr) {
            check_row_row(*a, *b, slug, uid, side);
        }
    }
}

/* FixedMeta: the control's everywhere except the summary row the edit touches;
 * there the +2 field count and +26 summary bit must equal Project's, and the
 * opaque +8..23 per-save blob stays the control's (the writer cannot reproduce
 * Project's fresh session token). */
void check_fixed_meta(const Bytes& control, const Bytes& project,
                      const Bytes& ours, std::size_t edited_row,
                      const char* slug) {
    const std::string prefix = std::string(slug);
    check_size(ours.size(), project.size(),
               (prefix + " FixedMeta size equals Project's").c_str());
    check_size(ours.size(), control.size(),
               (prefix + " FixedMeta size equals the control's").c_str());
    if (ours.size() != project.size() || ours.size() != control.size() ||
        ours.size() < 16) {
        return;
    }
    const std::size_t entry = meta_entry_for_row(control, edited_row);
    check(entry != kNoIndex,
          (prefix + " summary row has a FixedMeta entry").c_str());
    if (entry == kNoIndex) {
        return;
    }
    Bytes expected = control;
    std::size_t differences = 0;
    const std::size_t entries = (ours.size() - 16) / 47;
    for (std::size_t i = 0; i < entries; ++i) {
        const std::size_t at = 16 + i * 47;
        if (i == entry) {
            check_size(get_u16(ours, at + 2), get_u16(project, at + 2),
                       (prefix + " summary +2 field count").c_str());
            check(ours[at + 26] == project[at + 26],
                  (prefix + " summary +26 flag byte").c_str());
            for (std::size_t b = 8; b < 24; ++b) {
                check(ours[at + b] == control[at + b],
                      (prefix + " summary opaque blob is the control's").c_str());
                expected[at + b] = ours[at + b];
            }
            expected[at + 2] = project[at + 2];
            expected[at + 3] = project[at + 3];
            expected[at + 26] = project[at + 26];
        } else {
            for (std::size_t b = 0; b < 47; ++b) {
                if (ours[at + b] != control[at + b]) {
                    ++differences;
                }
            }
        }
    }
    check_size(differences, 0,
               (prefix + " untouched FixedMeta entries equal the control").c_str());
    check(ours == expected,
          (prefix + " FixedMeta equals the control except the summary row").c_str());
}

void check_pair(const PairCase& pair) {
    std::printf("-- pair %s:\n", pair.slug);
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / pair.control;
    const std::filesystem::path result_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / pair.result;
    if (!std::filesystem::exists(control_path) ||
        !std::filesystem::exists(result_path)) {
        std::printf("SKIP: %s / %s are not present\n", pair.control, pair.result);
        return;
    }

    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception& error) {
        check(false, "the pair control loads through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::TaskRow* moved = find_row(plan, pair.uid);
    const app::TaskRow* anchor = find_row(plan, pair.anchor_uid);
    check(moved != nullptr && anchor != nullptr,
          "the control has the moved row and its summary row");

    /* --- make_outline_edit: the measured words --------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_outline_edit(plan, control_path.string(),
                                              pair.uid, pair.indent, edit,
                                              edit_error);
    check(built, "make_outline_edit accepts the measured shape");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == (pair.indent ? app::StructuralKind::outline_indent
                                    : app::StructuralKind::outline_outdent),
          "the built edit has the outline kind");
    check_size(edit.uid, pair.uid, "the edit names the moved task");
    check_size(edit.anchor_uid, pair.anchor_uid,
               "the edit names the summary row the display touches");
    if (pair.indent) {
        check_size(edit.summary_duration, pair.summary_duration,
                   "the summary duration is Project's +84 (the calendar span)");
    } else {
        check_size(edit.restored_duration, pair.restored_duration,
                   "the restored duration is the marker's word");
        check_size(edit.restored_duration_format, pair.restored_duration_format,
                   "the restored duration format is the marker's word");
        check_size(app::packed_from_date32(edit.restored_start),
                   pair.restored_start_raw,
                   "the restored start is next_start(parent +104)");
        check_size(app::packed_from_date32(edit.restored_finish),
                   pair.restored_finish_raw,
                   "the restored finish is add_work(start, marker duration)");
    }

    /* --- push + display + undo/redo -------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the outline edit is queued");
    app::PlanInput shown = edits.apply(plan);
    const app::PlanInput expected =
        app::plan_input_from_document(mpp::load_file(result_path.string()));
    check_display_matches(shown, expected, pair.slug, "apply");

    check(edits.undo(), "the outline edit undoes");
    check_display_matches(edits.apply(plan), plan, pair.slug, "undo");
    // The shell undoes by rebuilding from the DISPLAYED plan (shown), not from
    // the file plan: without_structural_edits() must reset the rows the undone
    // edit owned, or the summary/level/duration would stay in the sheet.
    check_display_matches(edits.apply(edits.without_structural_edits(shown, plan)), plan,
                          pair.slug, "undo from the displayed plan");
    check(edits.redo(), "the outline edit redoes");
    check_display_matches(edits.apply(plan), expected, pair.slug, "redo");
    check_display_matches(edits.apply(edits.without_structural_edits(shown, plan)),
                          expected, pair.slug, "redo from the displayed plan");

    /* --- save through the real cascade ----------------------------------- */
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        (std::string("app-outline-") + pair.slug + ".mpp");
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the outline save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.outline_rows_written, 1,
               "the cascade wrote the outline edit");
    check(saved.warnings.empty(), "the outline save has no warnings");
    check(!std::filesystem::exists(out_path.string() + ".w51-structural-tmp"),
          "the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument project;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        project = mppwrite::CfbDocument::from_file(result_path.string());
        output = mppwrite::CfbDocument::from_file(out_path.string());
    } catch (const std::exception& open_error) {
        check(false, "the control, the Project pair and the port's save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    const Bytes control_fixed = control.read_stream(task_stream("FixedData"));
    const std::size_t rows =
        (control_fixed.size() - mppwrite::task_row::kHeaderSize) /
        mppwrite::task_row::kStride;
    const std::size_t edited_row = uid_row(control_fixed, pair.anchor_uid);
    check(edited_row != kNoIndex, "the summary row is a saved row");

    /* --- the six streams the writer owns --------------------------------- */
    check(output.read_stream(task_stream("FixedData")) ==
              project.read_stream(task_stream("FixedData")),
          "Task FixedData equals Project's save byte for byte");
    check(output.read_stream(task_stream("Fixed2Meta")) ==
              project.read_stream(task_stream("Fixed2Meta")),
          "Task Fixed2Meta equals Project's save byte for byte");
    check(output.read_stream(task_stream("VarMeta")) ==
              project.read_stream(task_stream("VarMeta")),
          "Task VarMeta equals Project's save byte for byte");
    check(output.read_stream(task_stream("Var2Data")) ==
              project.read_stream(task_stream("Var2Data")),
          "Task Var2Data equals Project's save byte for byte");
    if (edited_row != kNoIndex) {
        check_fixed_meta(control.read_stream(task_stream("FixedMeta")),
                         project.read_stream(task_stream("FixedMeta")),
                         output.read_stream(task_stream("FixedMeta")),
                         edited_row, pair.slug);
    }
    const Bytes output_fixed2 = output.read_stream(task_stream("Fixed2Data"));
    const Bytes project_fixed2 = project.read_stream(task_stream("Fixed2Data"));
    check_size(output_fixed2.size(), project_fixed2.size(),
               "Fixed2Data size equals Project's");
    if (output_fixed2.size() == project_fixed2.size()) {
        check(canonical_fixed2(output_fixed2, rows) ==
                  canonical_fixed2(project_fixed2, rows),
              "Fixed2Data equals Project's after identity canonicalisation");
        check(canonical_fixed2(output_fixed2, rows) !=
                  canonical_fixed2(control.read_stream(task_stream("Fixed2Data")),
                                   rows),
              "Fixed2Data outline bytes actually changed");
    }

    /* --- every other stream stays the control's -------------------------- */
    const std::vector<std::string> control_path_list = control.stream_paths();
    const std::vector<std::string> output_path_list = output.stream_paths();
    std::set<std::string> const control_paths(control_path_list.begin(),
                                              control_path_list.end());
    std::set<std::string> const output_paths(output_path_list.begin(),
                                             output_path_list.end());
    check(control_paths == output_paths,
          "the save adds and removes no stream");
    for (const std::string& path : control_paths) {
        if (owned_task_stream(path) || output_paths.count(path) == 0) {
            continue;
        }
        check(output.read_stream(path) == control.read_stream(path),
              "a non-owned stream changed against the control");
    }
    /* Project's own outline save also tombstones/re-creates the assignment row
     * (outline.h).  Report its row count rather than asserting it; the port
     * leaves TBkndAssn at the control's bytes, which the loop above proves. */
    std::size_t project_assn_rows = 0;
    std::size_t control_assn_rows = 0;
    if (project.has_stream("114/TBkndAssn/FixedData")) {
        project_assn_rows =
            project.read_stream("114/TBkndAssn/FixedData").size() / 110;
    }
    if (control.has_stream("114/TBkndAssn/FixedData")) {
        control_assn_rows =
            control.read_stream("114/TBkndAssn/FixedData").size() / 110;
    }
    std::printf("  %s: TBkndAssn rows control %llu, Project %llu "
                "(not written; documented in outline.h)\n",
                pair.slug,
                static_cast<unsigned long long>(control_assn_rows),
                static_cast<unsigned long long>(project_assn_rows));

    check(mppwrite::check_table(output).empty(),
          "the saved task table passes check_table");

    /* --- the reader and the display agree with Project's save ------------ */
    try {
        const mpp::Document reread_port = mpp::load_file(out_path.string());
        const mpp::Document reread_project = mpp::load_file(result_path.string());
        for (std::uint32_t uid = 1; uid <= 3; ++uid) {
            const mpp::Task* ours = find_task(reread_port, uid);
            const mpp::Task* theirs = find_task(reread_project, uid);
            check(ours != nullptr && theirs != nullptr,
                  "both saved files have the measured task");
            if (ours == nullptr || theirs == nullptr) {
                continue;
            }
            check_size(ours->outline_level, theirs->outline_level,
                       "the saved file reads back Project's outline level");
            check(ours->outline_number == theirs->outline_number,
                  "the saved file reads back Project's OutlineNumber");
            check(ours->summary == theirs->summary,
                  "the saved file reads back Project's summary flag");
            check_size(ours->child_count, theirs->child_count,
                       "the saved file reads back Project's child count");
            check_size(ours->duration_ticks, theirs->duration_ticks,
                       "the saved file reads back Project's duration");
            check(ours->start.raw == theirs->start.raw,
                  "the saved file reads back Project's +104 start");
            check(ours->finish.raw == theirs->finish.raw,
                  "the saved file reads back Project's +108 finish");
        }
        /* The display edit_model showed is the file the reader shows. */
        const app::PlanInput shown_again = edits.apply(plan);
        const app::PlanInput reread_plan =
            app::plan_input_from_document(reread_port);
        check_display_matches(shown_again, reread_plan, pair.slug, "reread");
        check_display_matches(reread_plan,
                              app::plan_input_from_document(reread_project),
                              pair.slug, "project");
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

void check_refusals() {
    const std::filesystem::path indent_control =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave28-outline-indent-control.mpp";
    const std::filesystem::path outdent_control =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave28-outline-outdent-control.mpp";
    if (!std::filesystem::exists(indent_control) ||
        !std::filesystem::exists(outdent_control)) {
        std::printf("SKIP: the refusal controls are not present\n");
        return;
    }
    app::PlanInput indent_plan;
    app::PlanInput outdent_plan;
    try {
        indent_plan =
            app::plan_input_from_document(mpp::load_file(indent_control.string()));
        outdent_plan = app::plan_input_from_document(
            mpp::load_file(outdent_control.string()));
    } catch (const std::exception& error) {
        check(false, "the refusal controls load");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    app::StructuralEdit edit;
    std::string error;

    check(!app::make_outline_edit(indent_plan, indent_control.string(), 1, true,
                                 edit, error),
          "the first top-level row has no preceding sibling to indent under");
    check(!error.empty(), "the refusal names its reason");

    check(!app::make_outline_edit(indent_plan, indent_control.string(), 1, false,
                                  edit, error),
          "a top-level leaf cannot be outdented");
    check(!app::make_outline_edit(outdent_plan, outdent_control.string(), 1,
                                  false, edit, error),
          "a summary cannot be outdented");
    check(!app::make_outline_edit(outdent_plan, outdent_control.string(), 2,
                                  true, edit, error),
          "indenting under an existing summary is not measured");
    check(!app::make_outline_edit(indent_plan, indent_control.string(), 99, true,
                                  edit, error),
          "an unknown uid is refused");

    /* push() re-checks the plan it is given: no plan, a wrong anchor, and a
     * pending delete of either row. */
    app::StructuralEdit good;
    check(app::make_outline_edit(indent_plan, indent_control.string(), 2, true,
                                 good, error),
          "the indent builds for the push checks");
    app::StructuralEdits no_plan;
    check(!no_plan.push(good, nullptr),
          "push refuses an outline edit without the plan");
    app::StructuralEdit wrong_anchor = good;
    wrong_anchor.anchor_uid = 3;
    app::StructuralEdits wrong;
    check(!wrong.push(wrong_anchor, &indent_plan),
          "push refuses an anchor that is not the row above");
    app::StructuralEdit remove_anchor;
    remove_anchor.kind = app::StructuralKind::delete_task;
    remove_anchor.uid = 1;
    app::StructuralEdits deleted_anchor;
    check(deleted_anchor.push(remove_anchor, &indent_plan),
          "the summary row delete is queued");
    check(!deleted_anchor.push(good, &indent_plan),
          "an outline edit naming a pending-deleted row is refused");

    /* Deleting either row of a queued outline edit displaces it; undo puts it
     * back. */
    app::StructuralEdits displaced;
    check(displaced.push(good, &indent_plan), "the outline edit is queued first");
    check(displaced.push(remove_anchor, &indent_plan),
          "the summary delete is queued over it");
    check_size(displaced.size(), 1, "the delete displaced the outline edit");
    if (displaced.size() == 1) {
        check_size(displaced.applied().front().displaced.size(), 1,
                   "the delete keeps the displaced outline edit");
    }
    check(displaced.undo(), "the delete undoes");
    check_size(displaced.size(), 1, "undo restores the displaced outline edit");
    if (displaced.size() == 1) {
        check(displaced.applied().front().kind ==
                  app::StructuralKind::outline_indent,
              "the restored edit is the outline indent");
    }
}

} /* namespace */

int main() {
    for (const PairCase& pair : kPairs) {
        check_pair(pair);
    }
    check_refusals();
    std::printf("app_edits_outline_tests: %d checks passed\n",
                g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_outline_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
