// app/app_edits_task_mode_tests.cpp -- the Task tab's Manually Schedule and
// Mode > Manually Scheduled through the app's edit model and save cascade
// against Project's own save pair (wave 30, W30-1.0).
//
// The committed pair is tests/data/wave30-task-mode-auto-control.mpp ->
// wave30-task-mode-manual.mpp: Project 2021 opened the control, selected task
// ID 1 (UniqueID 1, an automatically scheduled 24000-tick leaf) and pressed
// Manually Schedule, then saved.  wave30-task-mode-auto-control-null.mpp is
// Project's own no-edit save of the same control (the W29-1.0b mask source).
// The reverse pair (wave30-task-mode-manual-control.mpp -> -auto.mpp) is
// EVIDENCE only: Project also rescheduled the row there, and this port has no
// scheduler, so Auto Schedule stays disabled and make_task_mode_edit refuses
// the automatic direction.
//
// This test:
//
//   1. loads the control through the app's reader adapter and builds the edit
//      with make_task_mode_edit() -- the file-side shape check (the stored
//      precondition bytes and the whole-file conditions) plus the plan-side
//      checks;
//   2. queues it, proves the DISPLAY changes exactly the row's Manually
//      Scheduled flag (dates, duration and the other rows stay), and proves
//      undo/redo -- including undo from the DISPLAYED plan, which the shell
//      uses;
//   3. saves it with the real cascade and compares every stream under the
//      null-save discipline: the five owned streams and Props14 must equal
//      Project's save at every unmasked byte; every other stream must equal it
//      through the committed null save.  The pair's 19 effect bytes are also
//      checked by name and offset (Task FixedData +174; FixedMeta entry +17/
//      +18; Fixed2Meta +8; Fixed2Data +50/+54/+58; assignment Fixed2Meta +8;
//      the Props14 marker payload), and the total effect count must be 19.
//   4. reopens the port's save through the independent core/mpp reader and
//      proves the Manually Scheduled bit, dates and durations are the file
//      Project saved.
//
// It also checks every refusal the edit model and the writer owe the user:
// the project row, an unknown uid, a pending insert, a summary, an already
// manual row, a linked plan or file, progress anywhere, a resource-loaded
// assignment (plan and file), a task calendar, a sheet/file date mismatch, the
// Auto Schedule direction, the edit's exclusivity (only edit in the save, no
// start-date edit beside it), a hand-built queue that bypasses
// make_task_mode_edit, more than one writer edit, and a save-time refusal on a
// file patched after the edit was queued.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w30-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w30-app --config Release --target app_edits_task_mode_tests
//   ctest --test-dir build\w30-app -C Release -R "^app_edits_task_mode$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "layout_model.h"
// core/mppwrite/outline.h includes core/mppwrite/table.h from its own
// directory.  The app include path lists core/view first, so a bare "table.h"
// here would find core/view/table.h instead; outline.h pins the mppwrite header
// that task_row/check_table live in (the same rule as
// app_edits_outline_tests.cpp).  task_mode.h is the writer under test.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "save_pair_noise.h"
#include "table.h"
#include "task_mode.h"

#if !defined(APP_TEST_DATA_DIR) || !defined(APP_TEST_OUT_DIR)
int main() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
    return 0;
}
#else

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

/* TBkndTask geometry (table.h). */
constexpr std::size_t kFixedMetaHeader = 16;
constexpr std::size_t kFixedMetaEntry = 47;
constexpr std::size_t kFixed2MetaHeader = 16;
constexpr std::size_t kFixed2MetaEntry = 96;
constexpr std::size_t kFixed2Record = 64;

/* TBkndAssn geometry (task_mode.cpp). */
constexpr std::size_t kAssnStride = 110;
constexpr std::size_t kAssnMetaHeader = 16;
constexpr std::size_t kAssnMetaEntry = 34;
constexpr std::size_t kAssnFixed2MetaHeader = 16;
constexpr std::size_t kAssnFixed2MetaEntry = 53;
constexpr std::size_t kAssnFixed2Record = 48;

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

const char* kind_name(save_pair::Kind kind) {
    switch (kind) {
        case save_pair::Kind::fixed_block:
            return "block";
        case save_pair::Kind::exact:
            return "exact";
        case save_pair::Kind::props14:
            return "props14";
        case save_pair::Kind::cedl:
            return "cedl";
    }
    return "?";
}

save_pair::Kind kind_for_path(const std::string& path) {
    std::string name = path;
    if (!name.empty() && name.front() == '\x05') {
        name.erase(name.begin());
    }
    if (name == "DocumentSummaryInformation") {
        return save_pair::Kind::exact;
    }
    if (name == "Props14") {
        return save_pair::Kind::props14;
    }
    if (name == "214/CEdl/Var2Data") {
        return save_pair::Kind::cedl;
    }
    return save_pair::Kind::fixed_block;
}

std::string mismatch_message(const std::string& path,
                             const save_pair::Verdict& verdict) {
    std::string message =
        path + " equals Project's edit save outside the null-save mask";
    if (verdict.mismatches != 0) {
        char ours[8];
        char theirs[8];
        std::snprintf(ours, sizeof(ours), "%02x", verdict.output_byte);
        std::snprintf(theirs, sizeof(theirs), "%02x", verdict.result_byte);
        message += " (first of " + std::to_string(verdict.mismatches) +
                   " at byte " + std::to_string(verdict.first_mismatch) +
                   ": port 0x" + ours + ", Project 0x" + theirs + ")";
    }
    return message;
}

void report_stream(const std::string& path, save_pair::Kind kind,
                   const save_pair::Verdict& verdict) {
    std::printf("   %-34s %-7s noise=%-4llu effects=%-4llu",
                path.c_str(), kind_name(kind),
                static_cast<unsigned long long>(verdict.noise),
                static_cast<unsigned long long>(verdict.effects));
    if (verdict.masked_tail != 0) {
        std::printf(" masked-tail=%llu",
                    static_cast<unsigned long long>(verdict.masked_tail));
    }
    std::printf("\n");
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

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::string task_stream(const char* tail) {
    return std::string("114/TBkndTask/") + tail;
}

std::string assn_stream(const char* tail) {
    return std::string("114/TBkndAssn/") + tail;
}

Bytes stream_or_empty(mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

bool same_span(const Bytes& a, const Bytes& b, std::size_t at, std::size_t n) {
    if (at + n > a.size() || at + n > b.size()) {
        return false;
    }
    return std::equal(a.begin() + static_cast<std::ptrdiff_t>(at),
                      a.begin() + static_cast<std::ptrdiff_t>(at + n),
                      b.begin() + static_cast<std::ptrdiff_t>(at));
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

std::size_t meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < kFixedMetaHeader) {
        return kNoIndex;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(
        mppwrite::task_row::kHeaderSize + mppwrite::task_row::kStride * row);
    for (std::size_t i = 0; i < (meta.size() - kFixedMetaHeader) / kFixedMetaEntry;
         ++i) {
        if (get_u32(meta, kFixedMetaHeader + i * kFixedMetaEntry + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

std::size_t assn_uid_row(const Bytes& fixed, std::uint32_t uid) {
    if (fixed.size() < kAssnStride) {
        return kNoIndex;
    }
    for (std::size_t row = 0; row < fixed.size() / kAssnStride; ++row) {
        if (get_u32(fixed, kAssnStride * row + 4) == uid) {
            return row;
        }
    }
    return kNoIndex;
}

std::size_t assn_fixed2_meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < kAssnFixed2MetaHeader) {
        return kNoIndex;
    }
    const std::uint32_t offset =
        static_cast<std::uint32_t>(kAssnFixed2Record * row);
    for (std::size_t i = 0;
         i < (meta.size() - kAssnFixed2MetaHeader) / kAssnFixed2MetaEntry; ++i) {
        if (get_u32(meta, kAssnFixed2MetaHeader + i * kAssnFixed2MetaEntry + 4) ==
            offset) {
            return i;
        }
    }
    return kNoIndex;
}

/* The Props14 edit marker: locate the unique 0a 00 40 35 id, validate the
 * framing the writer measured and return the payload (0 or 1). */
bool marker_payload(const Bytes& props14, std::uint16_t& payload) {
    constexpr std::uint8_t kId[4] = {0x0A, 0x00, 0x40, 0x35};
    std::size_t found = kNoIndex;
    for (std::size_t at = 0; at + 4 <= props14.size(); ++at) {
        if (props14[at] != kId[0] || props14[at + 1] != kId[1] ||
            props14[at + 2] != kId[2] || props14[at + 3] != kId[3]) {
            continue;
        }
        if (found != kNoIndex) {
            return false; /* the measured marker occurs exactly once */
        }
        found = at;
    }
    if (found == kNoIndex || found < 4) {
        return false;
    }
    const std::size_t record = found - 4;
    if (record + 14 > props14.size()) {
        return false;
    }
    if (get_u32(props14, record) != 2 || get_u32(props14, record + 8) != 2) {
        return false;
    }
    payload = get_u16(props14, record + 12);
    return true;
}

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

/* One refused edit: make_task_mode_edit must say no, and (when `hand_built` is
 * also true) a StructuralEdits queue must refuse the same row even when the
 * caller bypasses the make function. */
void expect_make_refused(const app::PlanInput& plan, const std::string& source,
                         std::uint32_t uid, const char* what,
                         bool also_push = true) {
    app::StructuralEdit edit;
    std::string error;
    const bool built =
        app::make_task_mode_edit(plan, source, uid, true, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what);
        return;
    }
    if (also_push) {
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::set_task_mode;
        hand.uid = uid;
        hand.task_manual = true;
        app::StructuralEdits edits;
        check(!edits.push(std::move(hand), &plan),
              (std::string("push() refuses the same row: ") + what).c_str());
    }
}

/* A copy of `source` with one stream span rewritten.  Used to build the
 * imperfect files the writer must refuse; writes under APP_TEST_OUT_DIR. */
bool write_patched_copy(const std::filesystem::path& source,
                        const std::filesystem::path& target,
                        const std::string& stream, std::size_t offset,
                        const Bytes& bytes, std::string& failure) {
    try {
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(source.string());
        if (!document.has_stream(stream)) {
            failure = "the source has no " + stream;
            return false;
        }
        Bytes data = document.read_stream(stream);
        if (offset + bytes.size() > data.size()) {
            failure = "the patch is outside " + stream;
            return false;
        }
        std::copy(bytes.begin(), bytes.end(),
                  data.begin() + static_cast<std::ptrdiff_t>(offset));
        document.replace_stream(stream, data);
        std::error_code remove_error;
        std::filesystem::remove(target, remove_error);
        document.save(target.string());
        return true;
    } catch (const std::exception& error) {
        failure = error.what();
        return false;
    }
}

/* Replace one stream wholesale.  The empty link table's FixedData is 0 bytes in
 * the control, so a "linked" file has to GROW it, not patch inside it. */
bool write_stream_copy(const std::filesystem::path& source,
                       const std::filesystem::path& target,
                       const std::string& stream, const Bytes& bytes,
                       std::string& failure) {
    try {
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(source.string());
        if (!document.has_stream(stream)) {
            failure = "the source has no " + stream;
            return false;
        }
        document.replace_stream(stream, bytes);
        std::error_code remove_error;
        std::filesystem::remove(target, remove_error);
        document.save(target.string());
        return true;
    } catch (const std::exception& error) {
        failure = error.what();
        return false;
    }
}

Bytes le_bytes(std::uint32_t value) {
    Bytes out(4);
    put_u32(out, 0, value);
    return out;
}

/* --- the measured pair ---------------------------------------------------- */

void check_pair() {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave30-task-mode-auto-control.mpp";
    const std::filesystem::path result_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave30-task-mode-manual.mpp";
    const std::filesystem::path null_path =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        save_pair::null_fixture_name("wave30-task-mode-auto-control.mpp");
    if (!std::filesystem::exists(control_path) ||
        !std::filesystem::exists(result_path)) {
        std::printf("SKIP: the wave30 task-mode pair is not present\n");
        return;
    }
    check(std::filesystem::exists(null_path),
          "the committed null save for the task-mode control is present");

    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception& error) {
        check(false, "the task-mode control loads through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::TaskRow* row = find_row(plan, 1);
    check(row != nullptr, "the control has task uid 1");
    if (row == nullptr) {
        return;
    }
    check(!row->manually_scheduled,
          "the control's task uid 1 is automatically scheduled");
    check_size(row->duration_ticks, 24000, "the control's task uid 1 duration");
    check(row->start != app::kDateNa && row->finish != app::kDateNa,
          "the control's task uid 1 has both dates");

    /* --- make_task_mode_edit --------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_task_mode_edit(plan, control_path.string(), 1,
                                                true, edit, edit_error);
    check(built, "make_task_mode_edit accepts the measured shape");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::set_task_mode,
          "the built edit has the task-mode kind");
    check_size(edit.uid, 1, "the edit names task uid 1");
    check(edit.task_manual, "the edit is the manual direction");

    /* Auto Schedule is the reverse pair only; this port has no scheduler. */
    app::StructuralEdit auto_edit;
    std::string auto_error;
    check(!app::make_task_mode_edit(plan, control_path.string(), 1, false,
                                    auto_edit, auto_error),
          "the Auto Schedule direction is refused");
    check(auto_error.find("Auto Schedule") != std::string::npos,
          "the Auto Schedule refusal names the command");

    /* --- push + display + undo/redo -------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the task-mode edit is queued");
    const app::PlanInput shown = edits.apply(plan);
    const app::TaskRow* shown_row = find_row(shown, 1);
    check(shown_row != nullptr, "the display plan still has task uid 1");
    if (shown_row != nullptr) {
        check(shown_row->manually_scheduled,
              "the display shows task uid 1 as manually scheduled");
        check_size(shown_row->duration_ticks, row->duration_ticks,
                   "the display keeps the duration (the measured edit leaves it)");
        check(shown_row->start == row->start && shown_row->finish == row->finish,
              "the display keeps both dates (the measured edit leaves them)");
    }

    check(edits.undo(), "the task-mode edit undoes");
    const app::PlanInput undone = edits.apply(plan);
    const app::TaskRow* undone_row = find_row(undone, 1);
    check(undone_row != nullptr && !undone_row->manually_scheduled,
          "undo restores the automatic flag");
    const app::PlanInput undone_from_display =
        edits.apply(edits.without_structural_edits(shown, plan));
    const app::TaskRow* restored_row = find_row(undone_from_display, 1);
    check(restored_row != nullptr && !restored_row->manually_scheduled,
          "undo from the displayed plan restores the automatic flag");
    check(edits.redo(), "the task-mode edit redoes");
    const app::PlanInput redone = edits.apply(plan);
    const app::TaskRow* redone_row = find_row(redone, 1);
    check(redone_row != nullptr && redone_row->manually_scheduled,
          "redo shows the manually scheduled flag again");
    const app::PlanInput redone_from_display =
        edits.apply(edits.without_structural_edits(shown, plan));
    const app::TaskRow* redisplay_row = find_row(redone_from_display, 1);
    check(redisplay_row != nullptr && redisplay_row->manually_scheduled,
          "redo from the displayed plan keeps the manually scheduled flag");

    /* --- save through the real cascade ----------------------------------- */
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-task-mode-auto-manual.mpp";
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the task-mode save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.task_mode_written, 1, "the cascade wrote the task-mode edit");
    check(saved.api.find("write_task_mode") != std::string::npos,
          "the save's api names write_task_mode");
    check(saved.warnings.empty(), "the task-mode save has no warnings");
    check(!std::filesystem::exists(out_path.string() + ".w51-structural-tmp"),
          "the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument null_save;
    mppwrite::CfbDocument project;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        null_save = mppwrite::CfbDocument::from_file(null_path.string());
        project = mppwrite::CfbDocument::from_file(result_path.string());
        output = mppwrite::CfbDocument::from_file(out_path.string());
    } catch (const std::exception& open_error) {
        check(false, "the control, the null save, the Project pair and the "
                     "port's save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* --- the five owned effect streams ----------------------------------- */
    const std::array<std::string, 5> kOwned = {
        task_stream("FixedData"), task_stream("FixedMeta"),
        task_stream("Fixed2Meta"), task_stream("Fixed2Data"),
        assn_stream("Fixed2Meta")};
    std::size_t total_noise = 0;
    std::size_t total_effects = 0;
    for (const std::string& name : kOwned) {
        const save_pair::Verdict verdict = save_pair::compare(
            save_pair::Kind::fixed_block, control.read_stream(name),
            null_save.read_stream(name), project.read_stream(name),
            output.read_stream(name));
        report_stream(name, save_pair::Kind::fixed_block, verdict);
        total_noise += verdict.noise;
        total_effects += verdict.effects;
        check(verdict.lengths_ok,
              (name + ": control and null save have equal lengths").c_str());
        check(verdict.mismatches == 0,
              mismatch_message(name, verdict).c_str());
    }
    check_size(total_effects, 18,
               "the five owned streams carry the pair's 18 effect bytes");

    /* --- the owned effect bytes, by name and offset ---------------------- */
    const Bytes control_fixed = control.read_stream(task_stream("FixedData"));
    const Bytes project_fixed = project.read_stream(task_stream("FixedData"));
    const Bytes output_fixed = output.read_stream(task_stream("FixedData"));
    const std::size_t edited_row = uid_row(control_fixed, 1);
    check(edited_row != kNoIndex, "task uid 1 is a saved row in the control");
    const std::size_t edited_at =
        mppwrite::task_row::kHeaderSize +
        mppwrite::task_row::kStride * edited_row;
    check_size(get_u32(control_fixed, edited_at + 174), 0xFFFFFFFFu,
               "the control stores NA in FixedData +174 (StartAnchor)");
    check_size(get_u32(project_fixed, edited_at + 174), 0x3BF212C0u,
               "Project stores the effective start in FixedData +174");
    check(get_u32(output_fixed, edited_at + 174) == 0x3BF212C0u,
          "the port reproduces FixedData +174");

    const Bytes control_meta = control.read_stream(task_stream("FixedMeta"));
    const Bytes project_meta = project.read_stream(task_stream("FixedMeta"));
    const Bytes output_meta = output.read_stream(task_stream("FixedMeta"));
    const std::size_t meta_entry = meta_entry_for_row(control_meta, edited_row);
    check(meta_entry != kNoIndex, "the control has task uid 1's FixedMeta entry");
    if (meta_entry != kNoIndex) {
        const std::size_t at = kFixedMetaHeader + meta_entry * kFixedMetaEntry;
        check_size(control_meta[at + 17], 0x48,
                   "the automatic row's FixedMeta +17 is 0x48");
        check_size(control_meta[at + 18], 0xEF,
                   "the automatic row's FixedMeta +18 is 0xef");
        check(project_meta[at + 17] == 0x4C && output_meta[at + 17] == 0x4C,
              "Project's and the port's FixedMeta +17 are 0x4c");
        check(project_meta[at + 18] == 0xCF && output_meta[at + 18] == 0xCF,
              "Project's and the port's FixedMeta +18 are 0xcf");
    }

    const Bytes control_f2m = control.read_stream(task_stream("Fixed2Meta"));
    const Bytes project_f2m = project.read_stream(task_stream("Fixed2Meta"));
    const Bytes output_f2m = output.read_stream(task_stream("Fixed2Meta"));
    const std::size_t f2m_entry =
        meta_entry == kNoIndex ? kNoIndex : meta_entry;
    if (f2m_entry != kNoIndex) {
        const std::size_t at = kFixed2MetaHeader + f2m_entry * kFixed2MetaEntry;
        check_size(control_f2m[at + 8], 0x7F,
                   "the automatic row's Fixed2Meta +8 is 0x7f");
        check(project_f2m[at + 8] == 0xFF && output_f2m[at + 8] == 0xFF,
              "Project's and the port's Fixed2Meta +8 are 0xff");
    }

    const Bytes control_f2 = control.read_stream(task_stream("Fixed2Data"));
    const Bytes project_f2 = project.read_stream(task_stream("Fixed2Data"));
    const Bytes output_f2 = output.read_stream(task_stream("Fixed2Data"));
    const std::size_t f2_at = kFixed2Record * (edited_row + 3);
    if (f2_at + kFixed2Record <= control_f2.size()) {
        check_size(get_u32(control_f2, f2_at + 50), 0xFFFFFFFFu,
                   "the control's Fixed2Data +50 is NA");
        check_size(get_u32(control_f2, f2_at + 54), 0xFFFFFFFFu,
                   "the control's Fixed2Data +54 is NA");
        check_size(get_u32(control_f2, f2_at + 58), 0,
                   "the control's Fixed2Data +58 is 0");
        check(get_u32(project_f2, f2_at + 50) == 0x3BF212C0u &&
                  get_u32(output_f2, f2_at + 50) == 0x3BF212C0u,
              "Project's and the port's Fixed2Data +50 are the effective start");
        check(get_u32(project_f2, f2_at + 54) == 0x3BF627D8u &&
                  get_u32(output_f2, f2_at + 54) == 0x3BF627D8u,
              "Project's and the port's Fixed2Data +54 are the effective finish");
        check(get_u32(project_f2, f2_at + 58) == 0x00005DC0u &&
                  get_u32(output_f2, f2_at + 58) == 0x00005DC0u,
              "Project's and the port's Fixed2Data +58 are the duration");
    } else {
        check(false, "the control has task uid 1's Fixed2Data record");
    }

    const Bytes control_assn_fixed =
        control.read_stream(assn_stream("FixedData"));
    const Bytes control_assn_f2m =
        control.read_stream(assn_stream("Fixed2Meta"));
    const Bytes project_assn_f2m =
        project.read_stream(assn_stream("Fixed2Meta"));
    const Bytes output_assn_f2m = output.read_stream(assn_stream("Fixed2Meta"));
    const std::size_t assn_row = assn_uid_row(control_assn_fixed, 1);
    check(assn_row != kNoIndex, "the control has task uid 1's assignment");
    if (assn_row != kNoIndex) {
        const std::size_t entry =
            assn_fixed2_meta_entry_for_row(control_assn_f2m, assn_row);
        check(entry != kNoIndex,
              "the control has task uid 1's assignment Fixed2Meta entry");
        if (entry != kNoIndex) {
            const std::size_t at =
                kAssnFixed2MetaHeader + entry * kAssnFixed2MetaEntry;
            check_size(control_assn_f2m[at + 8], 0x4F,
                       "the automatic assignment's Fixed2Meta +8 is 0x4f");
            check(project_assn_f2m[at + 8] == 0xCF &&
                      output_assn_f2m[at + 8] == 0xCF,
                  "Project's and the port's assignment Fixed2Meta +8 are 0xcf");
        }
    }

    const Bytes control_props14 = control.read_stream("Props14");
    const Bytes project_props14 = project.read_stream("Props14");
    const Bytes output_props14 = output.read_stream("Props14");
    std::uint16_t control_marker = 0xFFFF;
    std::uint16_t project_marker = 0xFFFF;
    std::uint16_t output_marker = 0xFFFF;
    check(marker_payload(control_props14, control_marker),
          "the control has the measured Props14 edit marker");
    check(marker_payload(project_props14, project_marker),
          "Project's save has the measured Props14 edit marker");
    check(marker_payload(output_props14, output_marker),
          "the port's save has the measured Props14 edit marker");
    check_size(control_marker, 0, "the control's marker payload is 0");
    check_size(project_marker, 1, "Project's marker payload is 1");
    check_size(output_marker, 1, "the port's marker payload is 1");

    /* --- every other stream, through the committed null save ------------- */
    const std::set<std::string> owned_names = {
        task_stream("FixedData"), task_stream("FixedMeta"),
        task_stream("Fixed2Data"), task_stream("Fixed2Meta"),
        assn_stream("Fixed2Meta")};
    const std::vector<std::string> control_path_list = control.stream_paths();
    const std::vector<std::string> output_path_list = output.stream_paths();
    const std::set<std::string> control_paths(control_path_list.begin(),
                                              control_path_list.end());
    const std::set<std::string> output_paths(output_path_list.begin(),
                                             output_path_list.end());
    check(control_paths == output_paths, "the save adds and removes no stream");
    std::set<std::string> all_paths = control_paths;
    const std::vector<std::string> result_path_list = project.stream_paths();
    all_paths.insert(result_path_list.begin(), result_path_list.end());
    const std::vector<std::string> null_path_list = null_save.stream_paths();
    all_paths.insert(null_path_list.begin(), null_path_list.end());
    for (const std::string& path : all_paths) {
        if (owned_names.count(path) != 0) {
            continue;
        }
        const save_pair::Kind kind = kind_for_path(path);
        const save_pair::Verdict verdict = save_pair::compare(
            kind, stream_or_empty(control, path), stream_or_empty(null_save, path),
            stream_or_empty(project, path), stream_or_empty(output, path));
        report_stream(path, kind, verdict);
        total_noise += verdict.noise;
        total_effects += verdict.effects;
        if (kind == save_pair::Kind::exact) {
            check(verdict.noise_free,
                  (path + ": the null save leaves it byte for byte equal to "
                          "the control").c_str());
        } else {
            check(verdict.lengths_ok,
                  (path + ": control and null save have the measured lengths")
                      .c_str());
        }
        check(verdict.mismatches == 0,
              mismatch_message(path, verdict).c_str());
    }
    std::printf("   pair totals: noise=%llu effects=%llu\n",
                static_cast<unsigned long long>(total_noise),
                static_cast<unsigned long long>(total_effects));
    check_size(total_effects, 19,
               "the pair's whole-file effect count is the measured 19 bytes");

    check(mppwrite::check_table(output).empty(),
          "the saved task table passes check_table");

    /* The writer's own reader accepts its output. */
    mppwrite::TaskModeShape shape;
    std::string shape_error;
    check(mppwrite::read_task_mode_shape(control, 1, shape, shape_error),
          "read_task_mode_shape accepts the control");
    if (shape.uid == 1) {
        check_size(shape.duration_ticks, 24000, "the shape's duration");
        check_size(shape.start, 0x3BF212C0u, "the shape's effective start");
        check_size(shape.finish, 0x3BF627D8u, "the shape's effective finish");
    }
    check(!mppwrite::read_task_mode_shape(project, 1, shape, shape_error),
          "read_task_mode_shape refuses the already-manual result file");
    check(!mppwrite::read_task_mode_shape(output, 1, shape, shape_error),
          "read_task_mode_shape refuses the already-manual port output");

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
            check(ours->manually_scheduled == theirs->manually_scheduled,
                  "the saved file reads back Project's Manually Scheduled bit");
            check_size(ours->duration_ticks, theirs->duration_ticks,
                       "the saved file reads back Project's duration");
            check(ours->start.raw == theirs->start.raw,
                  "the saved file reads back Project's +104 start");
            check(ours->finish.raw == theirs->finish.raw,
                  "the saved file reads back Project's +108 finish");
        }
        const mpp::Task* port_task = find_task(reread_port, 1);
        check(port_task != nullptr && port_task->manually_scheduled,
              "task uid 1 reads back manually scheduled");
        const app::PlanInput reread_plan =
            app::plan_input_from_document(reread_port);
        const app::TaskRow* reread_row = find_row(reread_plan, 1);
        check(reread_row != nullptr && reread_row->manually_scheduled,
              "the display plan over the port's save is manual");
        check(reread_plan.tasks.size() == plan.tasks.size(),
              "the save keeps the task count");
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* --- refusals ------------------------------------------------------------- */

void check_refusals() {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave30-task-mode-auto-control.mpp";
    if (!std::filesystem::exists(control_path)) {
        std::printf("SKIP: the wave30 task-mode control is not present\n");
        return;
    }
    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception&) {
        check(false, "the refusal fixture loads");
        return;
    }
    const std::string source = control_path.string();

    /* The project row, an unknown uid, and the 0-uid hand-built queue. */
    expect_make_refused(plan, source, 0, "the project row is refused");
    expect_make_refused(plan, source, 999, "an unknown uid is refused");

    /* A pending insert: file_row 0 in the display plan. */
    {
        app::PlanInput pending = plan;
        for (app::TaskRow& row : pending.tasks) {
            if (row.uid == 1) {
                row.file_row = 0;
            }
        }
        expect_make_refused(pending, source, 1,
                            "a pending inserted row is refused");
    }

    /* A summary row. */
    {
        app::PlanInput summary = plan;
        for (app::TaskRow& row : summary.tasks) {
            if (row.uid == 2) {
                row.summary = true;
                row.outline_level = 1;
            }
        }
        expect_make_refused(summary, source, 2, "a summary row is refused");
    }

    /* An already manual row (uid 2 in the control is manual). */
    {
        app::StructuralEdit edit;
        std::string error;
        check(!app::make_task_mode_edit(plan, source, 2, true, edit, error),
              "an already manual row is refused");
        check(error.find("already manually scheduled") != std::string::npos,
              "the already-manual refusal names the reason");
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(source);
        mppwrite::TaskModeShape shape;
        std::string shape_error;
        check(!mppwrite::read_task_mode_shape(document, 2, shape, shape_error),
              "the writer refuses the already manual row too");
    }

    /* A linked plan. */
    {
        app::PlanInput linked = plan;
        app::LinkRow link;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        linked.links.push_back(link);
        expect_make_refused(linked, source, 1, "a linked plan is refused");
    }

    /* Progress anywhere in the plan. */
    {
        app::PlanInput progressed = plan;
        for (app::TaskRow& row : progressed.tasks) {
            if (row.uid == 2) {
                row.percent_complete = 50;
            }
        }
        expect_make_refused(progressed, source, 1,
                            "progress anywhere in the plan is refused");
    }

    /* A resource-loaded assignment in the plan. */
    {
        app::PlanInput assigned = plan;
        for (app::AssignmentRow& assignment : assigned.assignments) {
            if (assignment.task_uid == 1) {
                assignment.resource_uid = 7;
                assignment.work_ticks = 4800;
            }
        }
        expect_make_refused(assigned, source, 1,
                            "a resource-loaded assignment is refused");
    }

    /* A placeholder assignment whose planned work is not the measured task
     * duration (the control stores 24000 ticks of work for the 24000-tick
     * task). */
    {
        app::PlanInput worked = plan;
        for (app::AssignmentRow& assignment : worked.assignments) {
            if (assignment.task_uid == 1) {
                assignment.work_ticks = 0;
            }
        }
        expect_make_refused(
            worked, source, 1,
            "a placeholder assignment with different work is refused");
    }

    /* A task calendar in the plan. */
    {
        app::PlanInput calendar = plan;
        for (app::TaskRow& row : calendar.tasks) {
            if (row.uid == 1) {
                row.calendar_uid = 5;
            }
        }
        expect_make_refused(calendar, source, 1, "a task calendar is refused");
    }

    /* A sheet/file date mismatch (a pending Start edit): the file-side shape
     * passes, but the plan no longer matches the file the edit would write. */
    {
        app::PlanInput moved = plan;
        for (app::TaskRow& row : moved.tasks) {
            if (row.uid == 1) {
                row.start = row.start + 4800;
            }
        }
        expect_make_refused(moved, source, 1,
                            "a sheet/file date mismatch is refused", false);
    }

    /* push() with no plan. */
    {
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::set_task_mode;
        hand.uid = 1;
        hand.task_manual = true;
        app::StructuralEdits edits;
        check(!edits.push(std::move(hand), nullptr),
              "push() refuses a task-mode edit with no plan");
    }

    /* push() refuses the automatic direction. */
    {
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::set_task_mode;
        hand.uid = 1;
        hand.task_manual = false;
        app::StructuralEdits edits;
        check(!edits.push(std::move(hand), &plan),
              "push() refuses the automatic direction");
    }

    /* Exclusivity: a task-mode edit is the only edit in the save. */
    {
        app::StructuralEdit first;
        first.kind = app::StructuralKind::set_project_priority;
        first.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(first), &plan),
              "a priority edit queues on its own");
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::set_task_mode;
        hand.uid = 1;
        hand.task_manual = true;
        check(!edits.push(std::move(hand), &plan),
              "push() refuses a task-mode edit over another edit");
    }
    {
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::set_task_mode;
        hand.uid = 1;
        hand.task_manual = true;
        app::StructuralEdits edits;
        check(edits.push(std::move(hand), &plan),
              "a task-mode edit queues on an empty list");
        app::StructuralEdit second;
        second.kind = app::StructuralKind::set_task_mode;
        second.uid = 2;
        second.task_manual = true;
        check(!edits.push(std::move(second), &plan),
              "push() refuses a second task-mode edit");
    }

    /* The save cascade refuses a start-date edit beside a task-mode edit. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_task_mode_edit(plan, source, 1, true, edit, error),
              "the exclusivity fixture builds its task-mode edit");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan),
              "the exclusivity fixture queues the edit");
        std::error_code fs_error;
        std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                            fs_error);
        const std::filesystem::path out_path =
            std::filesystem::path(APP_TEST_OUT_DIR) /
            "app-task-mode-start-edit-refused.mpp";
        std::filesystem::remove(out_path, fs_error);
        app::StartEdit start;
        start.uid = 2;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved = app::save_structural_edits(
            edits, {start}, source, out_path.string(), false);
        check(!saved.ok,
              "the save refuses a task-mode edit with a start-date edit");
        check(saved.error.find("start-date") != std::string::npos,
              "the save's refusal names the start-date edit");
        check(!std::filesystem::exists(out_path),
              "the refused save writes no target file");
    }

    /* More than one writer edit throws. */
    {
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(source);
        std::vector<mppwrite::TaskModeEdit> two(2);
        two[0].uid = 1;
        two[1].uid = 2;
        bool threw = false;
        try {
            (void)mppwrite::write_task_mode(document, two);
        } catch (const mppwrite::Error&) {
            threw = true;
        } catch (const std::exception&) {
        }
        check(threw, "write_task_mode refuses more than one edit");
    }

    /* File-side refusals on files patched under APP_TEST_OUT_DIR.  Each patch
     * keeps the rest of the control intact, so the only difference the writer
     * sees is the one named condition. */
    std::error_code fs_error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        fs_error);
    const std::filesystem::path out_dir(APP_TEST_OUT_DIR);
    const std::filesystem::path control_fixed =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave30-task-mode-auto-control.mpp";

    /* A non-empty link table. */
    {
        const std::filesystem::path patched =
            out_dir / "app-task-mode-linked.mpp";
        std::string failure;
        check(write_stream_copy(control_fixed, patched,
                                "114/TBkndCons/FixedData",
                                Bytes{0, 0, 0, 0}, failure),
              "the linked-file patch is written");
        app::StructuralEdit edit;
        std::string error;
        check(!app::make_task_mode_edit(plan, patched.string(), 1, true, edit,
                                        error),
              "a file with link rows is refused");
        check(error.find("link rows") != std::string::npos,
              "the linked-file refusal names the link rows");
    }

    /* Progress in the edited row's FixedData (PercentComplete +92). */
    {
        const std::filesystem::path patched =
            out_dir / "app-task-mode-progress.mpp";
        std::string failure;
        const std::size_t fixed_offset =
            mppwrite::task_row::kHeaderSize +
            mppwrite::task_row::kStride; /* row 1 */
        check(write_patched_copy(control_fixed, patched,
                                 task_stream("FixedData"),
                                 fixed_offset + 92, Bytes{50, 0}, failure),
              "the progressed-file patch is written");
        app::StructuralEdit edit;
        std::string error;
        check(!app::make_task_mode_edit(plan, patched.string(), 1, true, edit,
                                        error),
              "a file with progress is refused");
        check(error.find("progress") != std::string::npos,
              "the progressed-file refusal names progress");
    }

    /* A task calendar in the edited row's FixedData (+178). */
    {
        const std::filesystem::path patched =
            out_dir / "app-task-mode-calendar.mpp";
        std::string failure;
        const std::size_t fixed_offset =
            mppwrite::task_row::kHeaderSize + mppwrite::task_row::kStride;
        check(write_patched_copy(control_fixed, patched,
                                 task_stream("FixedData"),
                                 fixed_offset + 178, le_bytes(5), failure),
              "the task-calendar patch is written");
        app::StructuralEdit edit;
        std::string error;
        check(!app::make_task_mode_edit(plan, patched.string(), 1, true, edit,
                                        error),
              "a file with a task calendar is refused");
        check(error.find("task calendar") != std::string::npos,
              "the task-calendar refusal names the calendar");
    }

    /* Placeholder planned work that is not the task duration.  The field is a
     * double at +20 (the control stores 2400000.0 = 2400 minutes for a
     * 24000-tick task); 1.0 is a shape no pair measured. */
    {
        const std::filesystem::path patched =
            out_dir / "app-task-mode-assignment-work.mpp";
        std::string failure;
        const Bytes one_double = {0, 0, 0, 0, 0, 0, 0xF0, 0x3F};
        check(write_patched_copy(control_fixed, patched,
                                 assn_stream("FixedData"), 20, one_double,
                                 failure),
              "the assignment-work patch is written");
        app::StructuralEdit edit;
        std::string error;
        check(!app::make_task_mode_edit(plan, patched.string(), 1, true, edit,
                                        error),
              "a file with different placeholder work is refused");
        check(error.find("planned work") != std::string::npos,
              "the assignment-work refusal names the planned work");
    }

    /* A REAL resource in the assignment row (+8 = 7).  The untouched plan still
     * says placeholder, so the plan-side check passes and the FILE-side check
     * (the writer's defence in depth) refuses.  The writer must leave the
     * document byte-identical, and the save cascade must fail rather than write
     * an unchanged file. */
    {
        const std::filesystem::path patched =
            out_dir / "app-task-mode-real-assignment.mpp";
        std::string failure;
        check(write_patched_copy(control_fixed, patched,
                                 assn_stream("FixedData"), 8, le_bytes(7),
                                 failure),
              "the real-assignment patch is written");
        app::StructuralEdit edit;
        std::string error;
        check(!app::make_task_mode_edit(plan, patched.string(), 1, true, edit,
                                        error),
              "a file with a real resource assignment is refused");
        check(error.find("placeholder") != std::string::npos,
              "the real-assignment refusal names the placeholder");

        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(patched.string());
        const Bytes before = document.to_bytes();
        std::vector<mppwrite::TaskModeEdit> one(1);
        one[0].uid = 1;
        const mppwrite::TaskModeWriteResult refused =
            mppwrite::write_task_mode(document, one);
        check_size(refused.rows_written, 0,
                   "the writer refuses the patched file");
        check(!refused.warnings.empty(),
              "the writer's refusal carries a warning");
        check(document.to_bytes() == before,
              "the refused writer call changes no byte");

        /* End to end: a hand-built queue passes push() (the plan is the
         * untouched control's) and the save must fail at step 5e. */
        app::StructuralEdit hand;
        hand.kind = app::StructuralKind::set_task_mode;
        hand.uid = 1;
        hand.task_manual = true;
        app::StructuralEdits edits;
        check(edits.push(std::move(hand), &plan),
              "the hand-built task-mode edit queues against the plan");
        const std::filesystem::path out_path =
            out_dir / "app-task-mode-save-refused.mpp";
        std::error_code remove_error;
        std::filesystem::remove(out_path, remove_error);
        const app::StructuralSaveResult saved = app::save_structural_edits(
            edits, {}, patched.string(), out_path.string(), false);
        check(!saved.ok, "the save fails on the patched file");
        check(!saved.error.empty(), "the failed save names the refusal");
        check(!std::filesystem::exists(out_path),
              "the failed save writes no target file");
        check(!std::filesystem::exists(out_path.string() + ".w51-structural-tmp"),
              "the failed save leaves no intermediate file");
    }
}

}  // namespace

int main() {
    check_pair();
    check_refusals();
    std::printf("app_edits_task_mode_tests: %d checks passed\n",
                g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_task_mode_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
