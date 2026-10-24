// app/app_edits_actuals_tests.cpp -- the actuals writer against Project's own
// save pairs (wave 28, W28-1.2 and W28-1.3).
//
// For each committed pair (tests/data/wave28-percent-*.mpp,
// wave28-mark-*.mpp, wave28-update*.mpp -- neutral metadata and the neutral
// C:/w28 drive path, see tests/data/README.md) this test:
//
//   1. loads the pair's CONTROL through the app's reader adapter, builds the
//      edit through make_percent_edit() (a % edit) or
//      make_update_actuals_edit() (Mark on Track / Update Project, W28-1.3),
//      queues it with push() and saves it with the real save cascade into
//      APP_TEST_OUT_DIR;
//   2. proves the four task-table streams match Project's own save:
//        FixedData  byte-identical to the pair,
//        VarMeta    byte-identical to the pair (the LOGICAL entry array: its
//                   header count, its entries, and the stream's original bytes
//                   past that count, which Project preserves),
//        Var2Data   byte-identical after masking the opaque per-session ids of
//                   the 0x0B40015E steps (actuals.h explains why they cannot
//                   be reproduced byte for byte),
//        FixedMeta  byte-identical to the CONTROL except the edited rows'
//                   variable-field counts, which must equal the pair's;
//   3. proves NO OTHER STREAM CHANGED: every stream outside those four is
//      byte-identical to the control.  Update Project's one extra owned word is
//      114/Props' status-date payload (0x02400045); the test proves the stream
//      equals the control except those four bytes, which must equal the pair's.
//      Project's own save also rewrites Fixed2Data/Fixed2Meta, TBkndAssn, CEdl,
//      the summary-information and props streams; the port deliberately owns
//      none of them (W28-0.4 precedent: Project's reopen is the acceptance test
//      for that half).
//
// The dates are not hand-fed: the save cascade computes them from the file's
// own calendar (core/cal), which is what makes this a test of the whole path
// rather than of write_percent_actuals() alone.  percent_through() -- the
// date-update arithmetic -- is also checked directly against the measured
// update pair (50/83/100 % from 12000/12000/9600 elapsed ticks).
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w28 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w28 --config Release
//   ctest --test-dir build\vs-w28 -C Release -R "^app_edits_actuals$" --output-on-failure

#include <algorithm>
#include <cmath>
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
#include "cal.h"
#include "cfb_writer.h"
#include "date.h"
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

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);
/* 114/Props record 0x02400045, kind 4, one packed date. */
constexpr std::uint32_t kStatusDateRecord = 0x02400045u;
/* The three W28-1.3 pairs update through Wed 2026-01-07 00:00. */
constexpr std::uint32_t kThroughRaw = 0x3BF40000u;

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

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
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
        return kNoIndex;
    }
    const std::size_t rows = (fixed_data.size() - 48) / 202;
    for (std::size_t row = 0; row < rows; ++row) {
        if (get_u32(fixed_data, 48 + 202 * row + 4) == uid) {
            return row;
        }
    }
    return kNoIndex;
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
        return kNoIndex;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(48 + 202 * row);
    for (std::size_t i = 0; i < (meta.size() - 16) / 47; ++i) {
        if (get_u32(meta, 16 + i * 47 + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

/* Payload offset of the project status-date record in a 114/Props stream, or
 * kNoIndex when the stream or the record is absent or malformed. */
std::size_t status_date_payload(const Bytes& props) {
    if (props.size() < 16) {
        return kNoIndex;
    }
    std::size_t offset = 16;
    while (offset + 12 <= props.size()) {
        const std::uint32_t length = get_u32(props, offset);
        const std::uint32_t id = get_u32(props, offset + 4);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (static_cast<std::uint64_t>(offset) + 12u + length >
            static_cast<std::uint64_t>(props.size())) {
            return kNoIndex;
        }
        if (id == kStatusDateRecord && length >= 4u) {
            return offset + 12;
        }
        offset += 12u + length;
    }
    return kNoIndex;
}

struct Pair {
    const char* slug;
    const char* control;
    const char* result;
    const std::uint32_t* uids;
    const std::uint16_t* percents;
    /* Expected elapsed ticks per uid; nullptr for a % edit (the writer derives
     * them from Duration * percent / 100, W28-1.2). */
    const std::uint32_t* ticks;
    std::size_t count;
    bool via_percent;        /* make_percent_edit vs make_update_actuals_edit */
    bool moves_status_date;  /* Update Project stores its date */
    std::uint32_t status_date_raw; /* control's stored date; 0 = unchecked */
};

const std::uint32_t kUid1[] = {1};
const std::uint32_t kUid2[] = {2};
const std::uint32_t kUid123[] = {1, 2, 3};

const std::uint16_t kPct0[] = {0};
const std::uint16_t kPct40[] = {40};
const std::uint16_t kPct100[] = {100};
const std::uint16_t kMarkManualPct[] = {83};
const std::uint16_t kUpdatePct[] = {50, 83, 100};

const std::uint32_t kTicks9600[] = {9600};
const std::uint32_t kTicks12000[] = {12000};
const std::uint32_t kUpdateTicks[] = {12000, 12000, 9600};

const Pair kPairs[] = {
    /* W28-1.1/W28-1.2: PerentComplete save pairs. */
    {"auto0-40", "wave28-percent-auto0-control.mpp",
     "wave28-percent-auto40.mpp", kUid1, kPct40, nullptr, 1, true, false, 0},
    {"auto40-100", "wave28-percent-auto40-control.mpp",
     "wave28-percent-auto100.mpp", kUid1, kPct100, nullptr, 1, true, false, 0},
    {"auto100-0", "wave28-percent-auto100-control.mpp",
     "wave28-percent-auto0.mpp", kUid1, kPct0, nullptr, 1, true, false, 0},
    {"manual0-40", "wave28-percent-manual0-control.mpp",
     "wave28-percent-manual40.mpp", kUid1, kPct40, nullptr, 1, true, false, 0},
    /* W28-1.3: Mark on Track (one row, status date left alone) and Update
     * Project (three rows, status date moved to the through date). */
    {"mark-auto", "wave28-mark-auto-control.mpp", "wave28-mark-auto.mpp",
     kUid1, kPct40, kTicks9600, 1, false, false, kThroughRaw},
    {"mark-manual", "wave28-mark-manual-control.mpp", "wave28-mark-manual.mpp",
     kUid2, kMarkManualPct, kTicks12000, 1, false, false, kThroughRaw},
    {"update", "wave28-update-control.mpp", "wave28-update.mpp",
     kUid123, kUpdatePct, kUpdateTicks, 3, false, true, 0xFFFFFFFFu},
};

void check_pair(const Pair& pair) {
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
    if (pair.status_date_raw != 0) {
        check(plan.status_date_stored,
              "the control stores a project status date");
        const app::date32 expected =
            pair.status_date_raw == 0xFFFFFFFFu
                ? app::kDateNa
                : app::date32_from_packed(pair.status_date_raw);
        check(plan.status_date == expected,
              "the reader decodes the control's status date");
    }

    const app::date32 through_date = core::sched::from_ymd(2026, 1, 7);
    app::StructuralEdit edit;
    std::string edit_error;
    if (pair.via_percent) {
        check(app::make_percent_edit(plan, pair.uids[0], pair.percents[0], edit,
                                     edit_error),
              "make_percent_edit accepts the saved leaf");
    } else {
        const std::vector<std::uint32_t> uids(pair.uids, pair.uids + pair.count);
        check(app::make_update_actuals_edit(plan, control_path.string(), uids,
                                            through_date, pair.moves_status_date,
                                            edit, edit_error),
              "make_update_actuals_edit accepts the saved leaves");
        check(edit.kind == app::StructuralKind::update_actuals,
              "the built edit is a date update");
        check(edit.through_date == through_date,
              "the edit carries the through date");
        check(edit.updates_status_date == pair.moves_status_date,
              "the edit carries the status-date decision");
        check_size(edit.actuals.size(), pair.count,
                   "the update covers every target");
        if (edit.actuals.size() == pair.count) {
            for (std::size_t i = 0; i < pair.count; ++i) {
                check_size(edit.actuals[i].uid, pair.uids[i],
                           "the target uid is the measured one");
                check_size(edit.actuals[i].percent, pair.percents[i],
                           "the target percent is Project's");
                if (pair.ticks != nullptr) {
                    check_size(edit.actuals[i].actual_ticks, pair.ticks[i],
                               "the target elapsed ticks are Project's");
                }
            }
        }
    }
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the actuals edit is queued");

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        (std::string("app-actuals-") + pair.slug + ".mpp");
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the actuals save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.percent_rows_written, pair.count,
               "every edited row's actuals were written");
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
    Bytes expected_meta = control_meta;
    bool meta_rows_ok = true;
    for (std::size_t i = 0; i < pair.count; ++i) {
        const std::size_t row = uid_row(control_fixed, pair.uids[i]);
        check(row != kNoIndex, "the edited uid is a saved row");
        if (row == kNoIndex) {
            meta_rows_ok = false;
            continue;
        }
        const std::size_t entry = meta_entry_for_row(control_meta, row);
        check(entry != kNoIndex, "the edited row has a FixedMeta entry");
        if (entry == kNoIndex) {
            meta_rows_ok = false;
            continue;
        }
        const std::size_t count_at = 16 + entry * 47 + 2;
        check_size(get_u16(output_meta, count_at), get_u16(result_meta, count_at),
                   "the row's variable-field count equals Project's");
        expected_meta[count_at] = result_meta[count_at];
        expected_meta[count_at + 1] = result_meta[count_at + 1];
    }
    check(meta_rows_ok, "every edited row has a control metadata entry");
    check(output_meta == expected_meta,
          "Task FixedMeta equals the control except the edited rows' field counts");

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

    /* --- the one props word Update Project owns -------------------------- */
    if (pair.moves_status_date || pair.status_date_raw != 0) {
        const Bytes control_props = control.read_stream("114/Props");
        const Bytes result_props = result.read_stream("114/Props");
        const Bytes output_props = output.read_stream("114/Props");
        const std::size_t control_at = status_date_payload(control_props);
        const std::size_t result_at = status_date_payload(result_props);
        const std::size_t output_at = status_date_payload(output_props);
        check(control_at != kNoIndex && result_at != kNoIndex &&
                  output_at != kNoIndex,
              "each props stream carries the status-date record");
        if (control_at != kNoIndex && result_at != kNoIndex &&
            output_at != kNoIndex) {
            if (pair.moves_status_date) {
                const std::uint32_t moved = get_u32(result_props, result_at);
                check(moved == kThroughRaw,
                      "Project's update pair stores the through date");
                check(get_u32(control_props, control_at) == 0xFFFFFFFFu,
                      "the update control stores no status date");
                check(get_u32(output_props, output_at) == moved,
                      "the port's save stores Project's status date");
                Bytes predicted = control_props;
                put_u32(predicted, control_at, moved);
                check(output_props == predicted,
                      "Props equals the control except the four status-date bytes");
            } else {
                check(get_u32(control_props, control_at) == pair.status_date_raw,
                      "the control's stored status date is the measured one");
                check(get_u32(result_props, result_at) == pair.status_date_raw,
                      "Project's save leaves the status date alone");
                check(get_u32(output_props, output_at) == pair.status_date_raw,
                      "the port's save leaves the status date alone");
            }
        }
    }

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
        if (path == "114/Props" && pair.moves_status_date) {
            continue;  /* the four status-date bytes are checked above */
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

    /* --- the reader agrees with Project's percent and status date -------- */
    try {
        const mpp::Document reread = mpp::load_file(out_path.string());
        for (std::size_t i = 0; i < pair.count; ++i) {
            bool found = false;
            for (const mpp::Task& task : reread.tasks) {
                if (task.uid != pair.uids[i]) {
                    continue;
                }
                found = true;
                check_size(task.percent_complete, pair.percents[i],
                           "the saved file reads back Project's percent");
            }
            check(found, "the edited uid is a live task in the saved file");
        }
        if (pair.moves_status_date || pair.status_date_raw != 0) {
            const std::uint32_t expected = pair.moves_status_date
                                               ? kThroughRaw
                                               : pair.status_date_raw;
            check(reread.project.status_date_stored,
                  "the save keeps the status-date record");
            check(reread.project.status_date.raw == expected,
                  "the saved file reads back the status date");
        }
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

const app::TaskRow* find_task(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

int shown_percent(const app::PlanInput& plan, std::uint32_t uid) {
    const app::TaskRow* row = find_task(plan, uid);
    return row == nullptr ? -1 : static_cast<int>(row->percent_complete);
}

/* percent_through(): the measured date-update arithmetic.  On the update
 * control the three rows are 0 % and every row's effective start is Sunday
 * 13:00; through Wed 2026-01-07 Project's save stores 50 %, 83 % (unrounded
 * 83.33333333333333 in the step block) and 100 % from 12000, 12000 and 9600
 * elapsed ticks. */
void check_percent_through() {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave28-update-control.mpp";
    if (!std::filesystem::exists(control_path)) {
        std::printf("SKIP: wave28-update-control.mpp is not present\n");
        return;
    }
    app::PlanInput plan;
    core::sched::Calendar calendar;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
        calendar = core::cal::load_project_calendar(control_path.string());
    } catch (const std::exception& error) {
        check(false, "the update control loads for the percent-through checks");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::date32 through_date = core::sched::from_ymd(2026, 1, 7);
    const std::uint32_t through = app::packed_from_date32(through_date);
    struct Expected {
        std::uint32_t uid;
        std::uint16_t percent;
        std::uint32_t ticks;
        double step;
    };
    const Expected expected[] = {
        {1, 50, 12000, 50.0},
        {2, 83, 12000, 12000.0 * 100.0 / 14400.0},
        {3, 100, 9600, 100.0},
    };
    for (const Expected& want : expected) {
        const app::TaskRow* row = find_task(plan, want.uid);
        check(row != nullptr, "the update control has the measured task");
        if (row == nullptr) {
            continue;
        }
        const std::uint32_t start = app::packed_from_date32(row->start);
        const std::uint32_t duration = static_cast<std::uint32_t>(row->duration_ticks);
        app::PercentThrough progress;
        check(app::percent_through(calendar, start, duration,
                                   row->manually_scheduled, through, progress),
              "percent_through measures the update date");
        check_size(progress.percent, want.percent,
                   "percent_through matches Project's percent");
        check_size(progress.actual_ticks, want.ticks,
                   "percent_through matches Project's elapsed ticks");
        check(std::fabs(progress.step_percent - want.step) < 1e-9,
              "percent_through carries the unrounded step percent");

        /* Through the row's own start is the 0 % shape: no actual time. */
        app::PercentThrough zero;
        check(app::percent_through(calendar, start, duration,
                                   row->manually_scheduled, start, zero),
              "percent_through accepts the row's start");
        check_size(zero.percent, 0, "a date at the start is 0 %");
        check_size(zero.actual_ticks, 0, "0 % carries no actual time");
    }
}

/* The edit model around a date update: one edit, many rows; last write wins
 * across kinds; a pending delete displaces the whole update and undo restores
 * it. */
void check_group_edit_model() {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave28-update-control.mpp";
    if (!std::filesystem::exists(control_path)) {
        std::printf("SKIP: wave28-update-control.mpp is not present\n");
        return;
    }
    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception& error) {
        check(false, "the update control loads for the edit-model checks");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::date32 through_date = core::sched::from_ymd(2026, 1, 7);
    const std::vector<std::uint32_t> uids = {1, 2, 3};
    app::StructuralEdit edit;
    std::string edit_error;
    check(app::make_update_actuals_edit(plan, control_path.string(), uids,
                                        through_date, true, edit, edit_error),
          "the three-row date update builds");
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the date update is queued");
    check_size(edits.size(), 1, "one edit represents the whole update");

    const app::PlanInput shown = edits.apply(plan);
    check(shown_percent(shown, 1) == 50 && shown_percent(shown, 2) == 83 &&
              shown_percent(shown, 3) == 100,
          "apply() shows Project's measured percents");
    check(edits.undo(), "the update undoes as one edit");
    const app::PlanInput restored = edits.apply(plan);
    check(shown_percent(restored, 1) == 0 && shown_percent(restored, 2) == 0 &&
              shown_percent(restored, 3) == 0,
          "undo restores the control's percents");
    check(edits.redo(), "the update redoes");
    const app::PlanInput again = edits.apply(plan);
    check(shown_percent(again, 1) == 50, "redo shows the measured percent again");

    /* A hand-built target the writer could not store is refused by push. */
    app::StructuralEdit broken = edit;
    broken.actuals[0].actual_ticks = 0;
    app::StructuralEdits broken_edits;
    check(!broken_edits.push(broken, &plan),
          "a date update whose ticks contradict its percent is refused");

    /* A pending delete of one target refuses a later update. */
    app::StructuralEdits delete_then_update;
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_task;
    remove.uid = 2;
    check(delete_then_update.push(remove, &plan), "the delete is queued");
    check(!delete_then_update.push(edit, &plan),
          "an update naming a pending-deleted task is refused");

    /* A delete over an existing update displaces the whole update; undo puts it
     * back. */
    app::StructuralEdits update_then_delete;
    check(update_then_delete.push(edit, &plan), "the update is queued first");
    check(update_then_delete.push(remove, &plan), "the delete is queued over it");
    check_size(update_then_delete.size(), 1,
               "the delete displaced the whole update");
    if (update_then_delete.size() == 1) {
        check_size(update_then_delete.applied().front().displaced.size(), 1,
                   "the delete keeps the displaced update");
    }
    check(update_then_delete.undo(), "the delete undoes");
    check_size(update_then_delete.size(), 1, "undo restores the displaced update");
    if (update_then_delete.size() == 1) {
        check(update_then_delete.applied().front().kind ==
                  app::StructuralKind::update_actuals,
              "the restored edit is the date update");
    }

    /* Last write wins across kinds, in the display AND in the file: a % edit
     * queued after the update owns uid 1, while uid 2/3 (and the status date)
     * still come from the update. */
    app::StructuralEdits mixed;
    check(mixed.push(edit, &plan), "the update is queued for the mixed save");
    app::StructuralEdit percent;
    check(app::make_percent_edit(plan, 1, 40, percent, edit_error),
          "the later percent edit builds");
    check(mixed.push(percent, &plan), "the percent edit is queued after it");
    const app::PlanInput mixed_shown = mixed.apply(plan);
    check(shown_percent(mixed_shown, 1) == 40 &&
              shown_percent(mixed_shown, 2) == 83 &&
              shown_percent(mixed_shown, 3) == 100,
          "apply() shows the last write per row across both kinds");

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-actuals-mixed.mpp";
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        mixed, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the mixed update + percent save succeeds");
    if (saved.ok) {
        check_size(saved.percent_rows_written, 3,
                   "the save writes one winning value per row");
        try {
            const mpp::Document reread = mpp::load_file(out_path.string());
            int percent1 = -1;
            int percent2 = -1;
            int percent3 = -1;
            for (const mpp::Task& task : reread.tasks) {
                if (task.uid == 1) {
                    percent1 = task.percent_complete;
                } else if (task.uid == 2) {
                    percent2 = task.percent_complete;
                } else if (task.uid == 3) {
                    percent3 = task.percent_complete;
                }
            }
            check(percent1 == 40 && percent2 == 83 && percent3 == 100,
                  "the saved file stores the last write per row");
            check(reread.project.status_date_stored &&
                      reread.project.status_date.raw == kThroughRaw,
                  "the mixed save still moves the project status date");
        } catch (const std::exception& read_error) {
            check(false, "the mixed save reopens through the reader");
            std::printf("READ ERROR: %s\n", read_error.what());
        }
    }
}

} /* namespace */

int main() {
    for (const Pair& pair : kPairs) {
        check_pair(pair);
    }
    check_percent_through();
    check_group_edit_model();
    std::printf("app_edits_actuals_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_actuals_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
