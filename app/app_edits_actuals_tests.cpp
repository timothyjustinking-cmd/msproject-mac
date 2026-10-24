// app/app_edits_actuals_tests.cpp -- the %-complete writer against Project's own
// save pairs (wave 28, W28-1.2).
//
// For each committed pair (tests/data/wave28-percent-*.mpp, neutral metadata and
// the neutral C:/w28 drive path -- see tests/data/README.md) this test:
//
//   1. loads the pair's CONTROL through the app's reader adapter, builds the
//      percent edit through make_percent_edit()/push() and saves it with the
//      real save cascade into APP_TEST_OUT_DIR;
//   2. proves the four task-table streams match Project's own save:
//        FixedData  byte-identical to the pair,
//        VarMeta    byte-identical to the pair (the LOGICAL entry array: its
//                   header count, its entries, and the stream's original bytes
//                   past that count, which Project preserves),
//        Var2Data   byte-identical after masking the opaque per-session ids of
//                   the 0x0B40015E steps (actuals.h explains why they cannot
//                   be reproduced byte for byte),
//        FixedMeta  byte-identical to the CONTROL except the edited row's
//                   variable-field count, which must equal the pair's;
//   3. proves NO OTHER STREAM CHANGED: every stream outside those four is
//      byte-identical to the control.  Project's own save also rewrites
//      Fixed2Data/Fixed2Meta, TBkndAssn, CEdl, the summary-information and
//      props streams; the port deliberately owns none of them (W28-0.4
//      precedent: Project's reopen is the acceptance test for that half).
//
// The dates are not hand-fed: the save cascade computes them from the file's
// own calendar (core/cal), which is what makes this a test of the whole path
// rather than of write_percent_actuals() alone.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w28 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w28 --config Release
//   ctest --test-dir build\vs-w28 -C Release -R "^app_edits_actuals$" --output-on-failure

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "actuals.h"
#include "cfb_writer.h"
#include "edit_model.h"
#include "layout_model.h"
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

bool owned_task_stream(const std::string& path) {
    return path == task_stream("FixedData") || path == task_stream("FixedMeta") ||
           path == task_stream("VarMeta") || path == task_stream("Var2Data");
}

std::size_t uid_row(const Bytes& fixed_data, std::uint32_t uid) {
    if (fixed_data.size() < 48) {
        return static_cast<std::size_t>(-1);
    }
    const std::size_t rows = (fixed_data.size() - 48) / 202;
    for (std::size_t row = 0; row < rows; ++row) {
        if (get_u32(fixed_data, 48 + 202 * row + 4) == uid) {
            return row;
        }
    }
    return static_cast<std::size_t>(-1);
}

/* Zero the u32 opaque token at the end of every 0x0B40015E step in a copy of
 * Var2Data.  The writer's save pairs then compare exactly: flags, percents and
 * everything else are still compared byte for byte. */
void mask_progress_ids(Bytes& var2, const Bytes& var_meta) {
    std::vector<mppwrite::VarEntry> entries;
    try {
        entries = mppwrite::read_var_entries(var_meta);
    } catch (const std::exception&) {
        return; /* the caller's equality check will report the damage */
    }
    for (const mppwrite::VarEntry& entry : entries) {
        if (entry.field_id != 0x0B40015Eu) {
            continue;
        }
        const std::size_t payload = static_cast<std::size_t>(entry.offset) + 4;
        if (payload + 16 > var2.size()) {
            continue;
        }
        const std::size_t count = get_u16(var2, payload);
        if (payload + 16 + 20 * count > var2.size()) {
            continue;
        }
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t id = payload + 16 + 20 * step + 16;
            std::fill(var2.begin() + static_cast<std::ptrdiff_t>(id),
                      var2.begin() + static_cast<std::ptrdiff_t>(id + 4),
                      std::uint8_t{0});
        }
    }
}

/* Index of the FixedMeta 47-byte entry that names this FixedData row. */
std::size_t meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < 16) {
        return static_cast<std::size_t>(-1);
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(48 + 202 * row);
    for (std::size_t i = 0; i < (meta.size() - 16) / 47; ++i) {
        if (get_u32(meta, 16 + i * 47 + 4) == offset) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

struct Pair {
    const char* slug;
    const char* control;
    const char* result;
    std::uint32_t uid;
    std::uint16_t percent;
};

const Pair kPairs[] = {
    {"auto0-40", "wave28-percent-auto0-control.mpp",
     "wave28-percent-auto40.mpp", 1, 40},
    {"auto40-100", "wave28-percent-auto40-control.mpp",
     "wave28-percent-auto100.mpp", 1, 100},
    {"auto100-0", "wave28-percent-auto100-control.mpp",
     "wave28-percent-auto0.mpp", 1, 0},
    {"manual0-40", "wave28-percent-manual0-control.mpp",
     "wave28-percent-manual40.mpp", 1, 40},
};

void check_pair(const Pair& pair) {
    std::printf("-- pair %s: uid %u -> %u %%\n", pair.slug,
                static_cast<unsigned>(pair.uid),
                static_cast<unsigned>(pair.percent));
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
    app::StructuralEdit edit;
    std::string edit_error;
    check(app::make_percent_edit(plan, pair.uid, pair.percent, edit, edit_error),
          "make_percent_edit accepts the saved leaf");
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the percent edit is queued");

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        (std::string("app-actuals-") + pair.slug + ".mpp");
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the percent save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.percent_rows_written, 1, "one row's actuals were written");
    check(!std::filesystem::exists(out_path.string() + ".w51-structural-tmp"),
          "the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument result;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        result = mppwrite::CfbDocument::from_file(result_path.string());
        output = mppwrite::CfbDocument::from_file(out_path.string());
    } catch (const std::exception& open_error) {
        check(false, "the control, the Project pair and the port's save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* --- the four streams the writer owns ------------------------------- */
    const Bytes control_fixed = control.read_stream(task_stream("FixedData"));
    const Bytes result_fixed = result.read_stream(task_stream("FixedData"));
    const Bytes output_fixed = output.read_stream(task_stream("FixedData"));
    check(output_fixed == result_fixed,
          "Task FixedData equals Project's save byte for byte");

    const Bytes control_meta = control.read_stream(task_stream("FixedMeta"));
    const Bytes result_meta = result.read_stream(task_stream("FixedMeta"));
    const Bytes output_meta = output.read_stream(task_stream("FixedMeta"));
    const std::size_t row = uid_row(control_fixed, pair.uid);
    check(row != static_cast<std::size_t>(-1), "the edited uid is a saved row");
    const std::size_t entry = row == static_cast<std::size_t>(-1)
                                  ? static_cast<std::size_t>(-1)
                                  : meta_entry_for_row(control_meta, row);
    check(entry != static_cast<std::size_t>(-1),
          "the edited row has a FixedMeta entry");
    if (entry != static_cast<std::size_t>(-1)) {
        const std::size_t count_at = 16 + entry * 47 + 2;
        check_size(get_u16(output_meta, count_at), get_u16(result_meta, count_at),
                   "the row's variable-field count equals Project's");
        Bytes expected_meta = control_meta;
        expected_meta[count_at] = result_meta[count_at];
        expected_meta[count_at + 1] = result_meta[count_at + 1];
        check(output_meta == expected_meta,
              "Task FixedMeta equals the control except the edited row's field count");
    }

    const Bytes control_var_meta = control.read_stream(task_stream("VarMeta"));
    const Bytes result_var_meta = result.read_stream(task_stream("VarMeta"));
    const Bytes output_var_meta = output.read_stream(task_stream("VarMeta"));
    check(output_var_meta == result_var_meta,
          "Task VarMeta equals Project's save byte for byte (entries and counts)");

    Bytes control_var2 = control.read_stream(task_stream("Var2Data"));
    Bytes result_var2 = result.read_stream(task_stream("Var2Data"));
    Bytes output_var2 = output.read_stream(task_stream("Var2Data"));
    mask_progress_ids(result_var2, result_var_meta);
    mask_progress_ids(output_var2, output_var_meta);
    check(output_var2 == result_var2,
          "Task Var2Data equals Project's save after masking the opaque step ids");

    /* --- no other stream changed against the control --------------------- */
    const std::vector<std::string> control_path_list = control.stream_paths();
    const std::vector<std::string> output_path_list = output.stream_paths();
    std::set<std::string> const control_paths(control_path_list.begin(),
                                              control_path_list.end());
    std::set<std::string> const output_paths(output_path_list.begin(),
                                             output_path_list.end());
    check(control_paths == output_paths,
          "the save adds and removes no stream");
    for (const std::string& path : control_paths) {
        if (owned_task_stream(path)) {
            continue;
        }
        if (output_paths.count(path) == 0) {
            check(false, "a control stream is missing from the save");
            continue;
        }
        check(output.read_stream(path) == control.read_stream(path),
              "a non-owned stream changed against the control");
    }

    check(mppwrite::check_table(output).empty(),
          "the saved task table passes check_table");

    /* --- the reader agrees with Project's percent ----------------------- */
    try {
        const mpp::Document reread = mpp::load_file(out_path.string());
        bool found = false;
        for (const mpp::Task& task : reread.tasks) {
            if (task.uid != pair.uid) {
                continue;
            }
            found = true;
            check_size(task.percent_complete, pair.percent,
                       "the saved file reads back Project's percent");
        }
        check(found, "the edited uid is a live task in the saved file");
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

} /* namespace */

int main() {
    for (const Pair& pair : kPairs) {
        check_pair(pair);
    }
    std::printf("app_edits_actuals_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_actuals_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
