// app/app_edits_new_task_mode_tests.cpp -- the project's new-task scheduling
// mode through the app's edit model and save cascade against Project's own save
// pairs (wave 28, W28-3.1).
//
// For each committed pair (tests/data/wave28-newtask-{manual-control,auto} and
// {auto-control,manual}; neutral metadata and the neutral C:/w28 drive path,
// see tests/data/README.md and core/mppwrite/project_props.h) this test:
//
//   1. reads the control's stored mode with mpp::read_new_task_mode and proves
//      make_new_task_mode_edit builds a set_new_task_mode edit for the OTHER
//      value, and that push()/undo()/redo() move it;
//   2. saves it with the real cascade into APP_TEST_OUT_DIR and compares the
//      port's 114/Props with Project's own save RECORD BY RECORD:
//        - the owned record 0x024013c8 (kind 2, 2 bytes) carries Project's
//          measured bytes: 0x00FF for Manually Scheduled, 0x0000 for Auto
//          Scheduled, in both directions;
//        - every other record is the control's, except Project's two per-save
//          opaque ids (0x02400029 / 0x024013c2, payload byte +3), which the
//          port does not own and the test masks in PROJECT's file only;
//   3. proves NO OTHER STREAM CHANGED: every stream outside 114/Props is
//      byte-identical to the control.  Project's own save rewrites
//      SummaryInformation, Props14, CEdl, the Fixed2Data session ids and the
//      214/Props/C* table caches; the port deliberately owns none of them (the
//      W28-1.2 precedent: Project's reopen of the port's save is the
//      acceptance test for that half).  The test reports Project's changed
//      stream count rather than asserting those bytes;
//   4. reopens the port's save through the independent core/mpp reader: the
//      project row's new_tasks_manual / new_tasks_manual_stored and
//      read_new_task_mode agree with Project's saved file, field by field;
//   5. checks Insert Task carries the project mode: a queued insert's display
//      row has edit.manually_scheduled, and the saved file's new row reads back
//      with the same Manual flag in BOTH directions (the Fixed2Meta bit is the
//      W45 measurement; this proves the app passes the option through);
//   6. checks the refusals: a file whose Props stream has the record removed is
//      refused by make_new_task_mode_edit, and a hand-pushed mode edit makes
//      the save FAIL (not a warning) without writing the target.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake --build build\w28-app --config Release --target app_edits_new_task_mode_tests
//   ctest --test-dir build\w28-app -C Release -R "^app_edits_new_task_mode$" --output-on-failure

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

#include "cfb_writer.h"
#include "edit_model.h"
#include "layout_model.h"
#include "reader.h"
#include "reader_adapter.h"
// core/mppwrite/project_props.h sits beside core/mppwrite/table.h; the app
// include path lists core/view first, so a bare "table.h" would find the wrong
// header.  project_props.h needs only cfb_writer.h, so it is safe to include;
// the writer's own header is what this test exercises.
#include "project_props.h"

#if !defined(APP_TEST_DATA_DIR) || !defined(APP_TEST_OUT_DIR)
int main() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
    return 0;
}
#else

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kNewTaskModeRecord = 0x024013C8u;
constexpr std::uint32_t kOpaqueRecordA = 0x02400029u;
constexpr std::uint32_t kOpaqueRecordB = 0x024013C2u;
constexpr std::size_t kPropsHeaderSize = 16;
constexpr std::size_t kRecordHeaderSize = 12;

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

struct PropsRecord {
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
    std::uint32_t length = 0;
    std::size_t offset = 0; /* the record header's stream offset */
};

/* The Props framing shared with core/mppwrite/actuals.cpp and project_props.cpp:
 * a 16-byte header, then u32 length / u32 id / u32 kind / payload, until the id
 * no longer carries 0x0040 in bits 16..23. */
std::vector<PropsRecord> parse_records(const Bytes& props) {
    std::vector<PropsRecord> records;
    std::size_t offset = kPropsHeaderSize;
    while (offset + kRecordHeaderSize <= props.size()) {
        PropsRecord record;
        record.length = get_u32(props, offset);
        record.id = get_u32(props, offset + 4);
        record.kind = get_u32(props, offset + 8);
        record.offset = offset;
        if ((record.id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (offset + kRecordHeaderSize + record.length > props.size()) {
            break; /* the caller's size checks report the damage */
        }
        records.push_back(record);
        offset += kRecordHeaderSize + record.length;
    }
    return records;
}

const PropsRecord* find_record(const std::vector<PropsRecord>& records,
                               std::uint32_t id) {
    for (const PropsRecord& record : records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

Bytes payload_of(const Bytes& props, const PropsRecord& record) {
    return Bytes(props.begin() + static_cast<std::ptrdiff_t>(
                                  record.offset + kRecordHeaderSize),
                 props.begin() + static_cast<std::ptrdiff_t>(
                                     record.offset + kRecordHeaderSize +
                                     record.length));
}

bool is_project_opaque_record(std::uint32_t id) {
    return id == kOpaqueRecordA || id == kOpaqueRecordB;
}

struct PairCase {
    const char* slug;
    const char* control;
    const char* result;
    bool control_manual;
    bool result_manual;
};

const PairCase kPairs[] = {
    /* Project saved its manual control, then the option changed to Auto. */
    {"manual_to_auto", "wave28-newtask-manual-control.mpp",
     "wave28-newtask-auto.mpp", true, false},
    /* A fresh session saved its auto control, then the option changed back. */
    {"auto_to_manual", "wave28-newtask-auto-control.mpp",
     "wave28-newtask-manual.mpp", false, true},
};

const app::TaskRow* find_row(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

const mpp::Task* highest_uid_task(const mpp::Document& document) {
    const mpp::Task* best = nullptr;
    for (const mpp::Task& task : document.tasks) {
        if (best == nullptr || task.uid > best->uid) {
            best = &task;
        }
    }
    return best;
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

    /* --- the control's stored mode and the edit make_new_task_mode_edit --- */
    const mpp::NewTaskMode control_mode =
        mpp::read_new_task_mode(control_path.string());
    check(control_mode.known, "the control stores the new-task mode");
    check(control_mode.manual == pair.control_manual,
          "the control's stored mode is the measurement's control value");

    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_new_task_mode_edit(
        control_path.string(), pair.result_manual, edit, edit_error);
    check(built, "make_new_task_mode_edit accepts a file that stores the record");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::set_new_task_mode,
          "the built edit has the new-task-mode kind");
    check(edit.new_tasks_manual == pair.result_manual,
          "the built edit carries the requested mode");
    check_size(edit.uid, 0, "the mode edit names no task");

    app::StructuralEdits edits;
    check(edits.push(edit), "the new-task-mode edit is queued without a plan");
    check(edits.size() == 1, "the queue holds the mode edit");
    const app::StructuralEdit* undo_edit = edits.next_undo();
    check(undo_edit != nullptr && undo_edit->kind ==
                                      app::StructuralKind::set_new_task_mode,
          "undo names the queued mode edit");
    check(edits.undo(), "the mode edit undoes");
    check(edits.empty(), "undo removes the mode edit");
    check(edits.redo(), "the mode edit redoes");
    check(edits.size() == 1, "redo puts the mode edit back");

    /* --- save through the real cascade ----------------------------------- */
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        (std::string("app-newtask-") + pair.slug + ".mpp");
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the new-task-mode save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.new_task_mode_written, 1,
               "the cascade wrote the new-task mode");
    check(saved.warnings.empty(), "the new-task-mode save has no warnings");
    check(saved.api.find("write_new_task_mode") != std::string::npos,
          "the save reports the new-task-mode writer");
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

    /* --- 114/Props record by record -------------------------------------- */
    const std::string props_path = "114/Props";
    const Bytes control_props = control.read_stream(props_path);
    const Bytes project_props = project.read_stream(props_path);
    const Bytes output_props = output.read_stream(props_path);
    check_size(output_props.size(), control_props.size(),
               "Props keeps the control's length");
    check_size(project_props.size(), control_props.size(),
               "Project's saved Props keeps the control's length");

    const std::vector<PropsRecord> control_records = parse_records(control_props);
    const std::vector<PropsRecord> project_records = parse_records(project_props);
    const std::vector<PropsRecord> output_records = parse_records(output_props);
    check_size(output_records.size(), control_records.size(),
               "the save adds and removes no Props record");
    check_size(project_records.size(), control_records.size(),
               "Project's save adds and removes no Props record");

    const PropsRecord* control_target = find_record(control_records, kNewTaskModeRecord);
    const PropsRecord* project_target = find_record(project_records, kNewTaskModeRecord);
    const PropsRecord* output_target = find_record(output_records, kNewTaskModeRecord);
    check(control_target != nullptr, "the control has the mode record");
    check(project_target != nullptr, "Project's save has the mode record");
    check(output_target != nullptr, "the port's save has the mode record");
    if (control_target == nullptr || project_target == nullptr ||
        output_target == nullptr) {
        return;
    }
    check_size(output_target->kind, 2, "the saved record is kind 2");
    check_size(output_target->length, 2, "the saved record is 2 bytes");
    check_size(output_target->kind, project_target->kind,
               "the saved record's kind is Project's");
    check_size(output_target->length, project_target->length,
               "the saved record's length is Project's");
    check_size(output_target->offset, project_target->offset,
               "the saved record's stream offset is Project's");

    const Bytes output_target_bytes = payload_of(output_props, *output_target);
    const Bytes project_target_bytes = payload_of(project_props, *project_target);
    check_size(output_target_bytes.size(), 2, "the saved payload is 2 bytes");
    check(output_target_bytes == project_target_bytes,
          "the saved mode payload equals Project's save byte for byte");
    const std::uint16_t expected = pair.result_manual ? 0x00FFu : 0x0000u;
    if (output_target_bytes.size() == 2) {
        check_size(get_u16(output_target_bytes, 0), expected,
                   "the saved mode payload is Project's measured word");
    }
    check(payload_of(control_props, *control_target) != output_target_bytes,
          "the control's mode payload actually changed");

    /* Every other record is the control's.  Project's own save also bumps the
     * two opaque per-save ids' payload byte +3; those bytes are masked in
     * PROJECT's file only. */
    for (const PropsRecord& record : output_records) {
        const PropsRecord* original = find_record(control_records, record.id);
        check(original != nullptr, "the port's save adds no Props record id");
        if (original == nullptr) {
            continue;
        }
        check_size(record.kind, original->kind, "an untouched record keeps its kind");
        check_size(record.length, original->length,
                   "an untouched record keeps its length");
        if (record.id == kNewTaskModeRecord) {
            continue;
        }
        check(payload_of(output_props, record) == payload_of(control_props, *original),
              "a non-owned Props record is the control's");
    }
    for (const PropsRecord& record : project_records) {
        const PropsRecord* original = find_record(control_records, record.id);
        if (original == nullptr || record.id == kNewTaskModeRecord) {
            continue;
        }
        Bytes project_payload = payload_of(project_props, record);
        Bytes control_payload = payload_of(control_props, *original);
        if (is_project_opaque_record(record.id) && project_payload.size() > 3 &&
            control_payload.size() > 3) {
            check(project_payload[3] != control_payload[3],
                  "Project's per-save opaque id changed as the pair records");
            project_payload[3] = control_payload[3];
        }
        check(project_payload == control_payload,
              "Project changed no Props record the port does not own");
    }

    /* --- every other stream stays the control's -------------------------- */
    const std::vector<std::string> control_path_list = control.stream_paths();
    const std::vector<std::string> output_path_list = output.stream_paths();
    std::set<std::string> const control_paths(control_path_list.begin(),
                                              control_path_list.end());
    std::set<std::string> const output_paths(output_path_list.begin(),
                                             output_path_list.end());
    check(control_paths == output_paths, "the save adds and removes no stream");
    std::size_t changed_streams = 0;
    for (const std::string& path : control_paths) {
        if (output_paths.count(path) == 0) {
            continue;
        }
        if (output.read_stream(path) != control.read_stream(path)) {
            ++changed_streams;
        }
        if (path == props_path) {
            continue;
        }
        check(output.read_stream(path) == control.read_stream(path),
              "a stream outside 114/Props changed against the control");
    }
    check_size(changed_streams, 1,
               "the port's save changes exactly one stream (114/Props)");
    /* Report, do not assert, the part Project's own save rewrites. */
    std::size_t project_changed_streams = 0;
    for (const std::string& path : project.stream_paths()) {
        if (control.has_stream(path) &&
            project.read_stream(path) != control.read_stream(path)) {
            ++project_changed_streams;
        }
    }
    std::printf("  %s: Project's own save changed %llu stream(s); the port owns "
                "114/Props only\n",
                pair.slug,
                static_cast<unsigned long long>(project_changed_streams));

    /* The task tables are not this writer's; the stream loop above proves they
     * are byte-identical to the control, so no check_table call is needed. */

    /* --- the reader agrees with Project's save, field by field ----------- */
    const mpp::NewTaskMode project_mode =
        mpp::read_new_task_mode(result_path.string());
    const mpp::NewTaskMode output_mode = mpp::read_new_task_mode(out_path.string());
    check(project_mode.known, "Project's save stores the mode");
    check(output_mode.known, "the port's save stores the mode");
    check(project_mode.manual == pair.result_manual,
          "Project's save reads back the measured mode");
    check(output_mode.manual == pair.result_manual,
          "the port's save reads back the measured mode");
    try {
        const mpp::Document reread_port = mpp::load_file(out_path.string());
        const mpp::Document reread_project = mpp::load_file(result_path.string());
        check(reread_port.project.new_tasks_manual_stored,
              "the project row says the port's save stores the mode");
        check(reread_port.project.new_tasks_manual_stored ==
                  reread_project.project.new_tasks_manual_stored,
              "the stored flag equals Project's");
        check(reread_port.project.new_tasks_manual ==
                  reread_project.project.new_tasks_manual,
              "the project row's mode equals Project's save");
        check(reread_port.project.new_tasks_manual == pair.result_manual,
              "the project row's mode is the measured one");
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* Insert Task carries the project's mode (W28-3.1): the display row and the
 * saved row both take edit.manually_scheduled.  Run in BOTH directions on both
 * controls, so the pass-through is proved independent of the file's own mode. */
void check_insert_carries_mode(const char* control_name, bool manual) {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / control_name;
    if (!std::filesystem::exists(control_path)) {
        std::printf("SKIP: %s is not present\n", control_name);
        return;
    }
    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception& load_error) {
        check(false, "the insert control loads");
        std::printf("LOAD ERROR: %s\n", load_error.what());
        return;
    }

    app::StructuralEdits edits;
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::insert_task;
    edit.uid = edits.next_placeholder_uid(plan);
    edit.anchor_uid = 0;
    edit.name = "Inserted Task";
    edit.manually_scheduled = manual;
    check(edit.uid != 0 && edits.push(edit),
          "the insert with a scheduling mode is queued");

    const app::PlanInput shown = edits.apply(plan);
    const app::TaskRow* placeholder = find_row(shown, edit.uid);
    check(placeholder != nullptr, "the display plan has the inserted row");
    if (placeholder != nullptr) {
        check(placeholder->manually_scheduled == manual,
              "the displayed inserted row carries the mode");
    }

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        (std::string("app-newtask-insert-") + (manual ? "manual" : "auto") + ".mpp");
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the insert save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.added, 1, "the cascade added one row");
    check_size(saved.added_uids.size(), 1, "the writer reported the new uid");
    try {
        const mpp::Document reread = mpp::load_file(out_path.string());
        const mpp::Task* added = highest_uid_task(reread);
        check(added != nullptr, "the saved file has the inserted row");
        if (added != nullptr) {
            check_size(added->uid,
                       saved.added_uids.empty() ? 0 : saved.added_uids.front(),
                       "the inserted row is the writer's new uid");
            check(added->manually_scheduled == manual,
                  "the saved inserted row carries the project's mode");
        }
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the insert save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* A file without 114/Props record 0x024013c8: the app refuses to queue the
 * change, and a hand-built edit's save fails instead of writing a file whose
 * option would be invented. */
void check_missing_record_refusal() {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave28-newtask-manual-control.mpp";
    if (!std::filesystem::exists(control_path)) {
        std::printf("SKIP: the refusal control is not present\n");
        return;
    }
    mppwrite::CfbDocument document;
    try {
        document = mppwrite::CfbDocument::from_file(control_path.string());
    } catch (const std::exception& open_error) {
        check(false, "the refusal control opens");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }
    Bytes props = document.read_stream("114/Props");
    const std::vector<PropsRecord> records = parse_records(props);
    const PropsRecord* target = find_record(records, kNewTaskModeRecord);
    check(target != nullptr, "the refusal control has the mode record");
    if (target == nullptr) {
        return;
    }
    props.erase(props.begin() + static_cast<std::ptrdiff_t>(target->offset),
                props.begin() + static_cast<std::ptrdiff_t>(
                                    target->offset + kRecordHeaderSize +
                                    target->length));
    document.replace_stream("114/Props", props);

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path no_record_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-newtask-no-record.mpp";
    std::filesystem::remove(no_record_path, error);
    document.save(no_record_path.string());

    const mpp::NewTaskMode mode = mpp::read_new_task_mode(no_record_path.string());
    check(!mode.known, "the spliced file no longer stores the mode");

    app::StructuralEdit edit;
    std::string edit_error;
    check(!app::make_new_task_mode_edit(no_record_path.string(), false, edit,
                                        edit_error),
          "make_new_task_mode_edit refuses a file without the record");
    check(!edit_error.empty() && edit_error.find("0x024013c8") != std::string::npos,
          "the refusal names the missing record");

    app::StructuralEdit hand;
    hand.kind = app::StructuralKind::set_new_task_mode;
    hand.new_tasks_manual = false;
    app::StructuralEdits edits;
    check(edits.push(hand), "a hand-built mode edit still queues");
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        "app-newtask-no-record-out.mpp";
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, no_record_path.string(), out_path.string(), false);
    check(!saved.ok, "the save of a file without the record fails");
    check(saved.error.find("0x024013c8") != std::string::npos,
          "the save error names the missing record");
    check(!std::filesystem::exists(out_path.string()),
          "the failed save does not write the target");
    check(!std::filesystem::exists(out_path.string() + ".w51-structural-tmp"),
          "the failed save leaves no intermediate file");
}

}  // namespace

int main() {
    for (const PairCase& pair : kPairs) {
        check_pair(pair);
    }
    check_insert_carries_mode("wave28-newtask-manual-control.mpp", true);
    check_insert_carries_mode("wave28-newtask-manual-control.mpp", false);
    check_insert_carries_mode("wave28-newtask-auto-control.mpp", true);
    check_insert_carries_mode("wave28-newtask-auto-control.mpp", false);
    check_missing_record_refusal();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#endif  // APP_TEST_DATA_DIR / APP_TEST_OUT_DIR
