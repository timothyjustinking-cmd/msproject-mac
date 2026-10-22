// app/document_tests.cpp -- the save helper, proved headlessly.
//
// Three layers:
//   1. the pure parts -- default_save_name() and collect_start_edits() -- on
//      synthetic rows, always;
//   2. the real start-date write: take the committed synthetic fixture
//      (tests/data/synthetic-base.mpp, NOT client data -- see tests/data/
//      README.md), edit one task's Start through the sheet model exactly as the
//      shell's keyboard does, write the edit set to a file under the build tree,
//      then RE-READ that file with the .mpp reader and check the pin landed;
//   3. wave 15: the two non-table save operations, on the same fixture --
//        (a) a 214/CTable date-format edit (payload +7 changes, nothing else in
//            214/ does),
//        (b) a 214/CTable column replacement (one 115-byte record, count kept),
//        (c) a save with no pending edit (214/ and 114/Props byte-identical),
//        (d) a custom-field formula set (core/props reads it back, flag = 1),
//        (e) formulas cleared while a Text1 value remains (flag restored to 1,
//            value still readable), and
//        (f) a refused view-state edit (bad column index: save succeeds, 214/
//            unchanged, a warning explains it).
//
// The source fixture is never written to: the helper refuses target == source,
// and the tests assert the fixture's size is unchanged afterwards.
//
// What this cannot prove: that MS Project itself accepts a written file or
// honours the written pin/table state when it opens it.  The date pin was
// measured through COM in W29 (memory/writer.md: `pin_task_start` moved uid 1 and
// Project's recalc kept it); the 214/ writer's mutations are the wave-14 oracle's
// measured edits (docs/analysis/oracle/w14-oracle.md section 1).  Neither is
// repeated here -- there is no Project on this box and none on the runner.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w64-app -G "Visual Studio 17 2022" -A x64
//   cmake --build build\w64-app --config Release
//   ctest --test-dir build\w64-app -C Release -R "^app_document$" --output-on-failure

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "add_delete.h"
#include "cfb_writer.h"
#include "date.h"
#include "document.h"
#include "edit_model.h"
#include "field_values.h"
#include "layout_model.h"
#include "props.h"
#include "reader_adapter.h"
#include "sheet.h"
#include "table.h"
#include "viewstate.h"

// The hand-built reference cascade below mirrors whichever final write
// app/document.cpp compiles (see app/CMakeLists.txt's APP_MPPWRITE_SAVE_HEADER).
#if APP_MPPWRITE_SAVE_HEADER
#include "save.h"
#else
#include "row_writer.h"
#endif

namespace {

using app::date32;
using app::kDateNa;
using core::sched::from_ymd;

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

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

void check_date(date32 got, date32 want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %s want %s\n", what, core::sched::to_string(got).c_str(),
                    core::sched::to_string(want).c_str());
    }
}

// Not called file_size(): that name would collide with std::filesystem::file_size
// through argument-dependent lookup on a path argument.
std::size_t bytes_on_disk(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

std::vector<std::uint8_t> file_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

app::TaskRow make_task(std::uint32_t uid, date32 start) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.name = "Task";
    row.duration_ticks = 4800;
    row.start = start;
    row.finish = start;
    return row;
}

// ---------------------------------------------------------------------------
// Helpers for the wave-15 save operations
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)

const std::filesystem::path kFixture =
    std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
const std::filesystem::path kOutDir = std::filesystem::path(APP_TEST_OUT_DIR);

std::filesystem::path out_path(const char* name) {
    std::error_code error;
    std::filesystem::create_directories(kOutDir, error);
    const std::filesystem::path path = kOutDir / name;
    std::filesystem::remove(path, error);
    return path;
}

using StreamMap = std::vector<std::pair<std::string, std::vector<std::uint8_t>>>;

// Every stream under `prefix`, normalised paths, sorted by path.  Throws on a
// file the writer's CFB layer cannot read (the tests catch that once).
StreamMap streams_with_prefix(const std::filesystem::path& path, const std::string& prefix) {
    const mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(path.string());
    StreamMap out;
    for (const std::string& stream_path : document.stream_paths()) {
        if (stream_path.rfind(prefix, 0) == 0) {
            out.emplace_back(stream_path, document.read_stream(stream_path));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return out;
}

std::vector<std::uint8_t> stream_bytes(const std::filesystem::path& path,
                                       const std::string& stream_path) {
    const mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(path.string());
    return document.read_stream(stream_path);
}

// True when both maps hold the same stream names and the same bytes, except that
// `ignore` (when non-empty) is allowed to differ.  `why` names the first
// difference for the failure message.
bool same_stream_bytes(const StreamMap& before, const StreamMap& after,
                       const std::string& ignore, std::string& why) {
    if (before.size() != after.size()) {
        why = "stream count " + std::to_string(before.size()) + " -> " +
              std::to_string(after.size());
        return false;
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i].first != after[i].first) {
            why = "stream set changed: " + before[i].first + " -> " + after[i].first;
            return false;
        }
        if (before[i].first == ignore) {
            continue;
        }
        if (before[i].second != after[i].second) {
            why = before[i].first + " changed";
            return false;
        }
    }
    return true;
}

std::vector<std::size_t> differing_offsets(const std::vector<std::uint8_t>& lhs,
                                           const std::vector<std::uint8_t>& rhs) {
    std::vector<std::size_t> offsets;
    const std::size_t common = std::min(lhs.size(), rhs.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (lhs[i] != rhs[i]) {
            offsets.push_back(i);
        }
    }
    for (std::size_t i = common; i < std::max(lhs.size(), rhs.size()); ++i) {
        offsets.push_back(i);
    }
    return offsets;
}

// The 0x02401392 "custom-field data in use" flag from a raw 114/Props stream.
bool props_flag(const std::vector<std::uint8_t>& props, std::uint16_t& value) {
    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    if (!msproject::props::parse_props(props, document, &error)) {
        return false;
    }
    const msproject::props::PropsRecord* record =
        document.find(msproject::props::kCustomFieldFlagRecordId);
    if (record == nullptr || record->kind != 2u || record->payload_length != 2u) {
        return false;
    }
    const std::size_t at = static_cast<std::size_t>(record->offset) + 12u;
    if (at + 2u > props.size()) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(props[at]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(props[at + 1]) << 8));
    return true;
}

struct ParsedCTable {
    bool ok = false;
    msproject::viewstate::ViewState state;
    std::string error;
};

ParsedCTable parse_ctable_at(const std::filesystem::path& path) {
    ParsedCTable out;
    try {
        const mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(path.string());
        const std::vector<std::uint8_t> fixed_data =
            document.read_stream("214/CTable/FixedData");
        const std::vector<std::uint8_t> fixed_meta =
            document.read_stream("214/CTable/FixedMeta");
        const std::vector<std::uint8_t> var_meta =
            document.read_stream("214/CTable/VarMeta");
        const std::vector<std::uint8_t> var2_data =
            document.read_stream("214/CTable/Var2Data");
        msproject::viewstate::PartSet parts;
        parts.fixed_data = fixed_data;
        parts.fixed_meta = fixed_meta;
        parts.var_meta = var_meta;
        parts.var2_data = var2_data;
        msproject::viewstate::ParseError error;
        out.ok = msproject::viewstate::parse_view_state("CTable", parts, out.state, &error);
        if (!out.ok) {
            out.error = error.message;
        }
    } catch (const std::exception& error) {
        out.error = error.what();
    }
    return out;
}

void print_outcome(const char* label, const app::SaveOutcome& outcome) {
    std::printf("SAVE(%s): api=%s ok=%d view=%d formula=%d flag=%d\n", label,
                outcome.api.c_str(), outcome.ok ? 1 : 0,
                outcome.view_state_written ? 1 : 0, outcome.formula_written ? 1 : 0,
                outcome.custom_field_flag_restored ? 1 : 0);
    for (const std::string& warning : outcome.warnings) {
        std::printf("SAVE(%s) WARNING: %s\n", label, warning.c_str());
    }
}

// ---------------------------------------------------------------------------
// Helpers for the wave-17 structural cascade
// ---------------------------------------------------------------------------

void print_cascade(const char* label, const app::StructuralSaveResult& outcome) {
    std::printf("CASCADE(%s): api=%s ok=%d added=%llu deleted=%llu fields=%llu starts=%llu "
                "doc_edits=%d/%d view=%d formula=%d flag=%d\n",
                label, outcome.api.c_str(), outcome.ok ? 1 : 0,
                static_cast<unsigned long long>(outcome.added),
                static_cast<unsigned long long>(outcome.deleted),
                static_cast<unsigned long long>(outcome.fields_written),
                static_cast<unsigned long long>(outcome.start_rows_written),
                outcome.document_edits_requested ? 1 : 0,
                outcome.document_edits_written ? 1 : 0,
                outcome.document_edits.view_state_written ? 1 : 0,
                outcome.document_edits.formula_written ? 1 : 0,
                outcome.document_edits.custom_field_flag_restored ? 1 : 0);
    for (const std::string& warning : outcome.warnings) {
        std::printf("CASCADE(%s) WARNING: %s\n", label, warning.c_str());
    }
    if (!outcome.error.empty()) {
        std::printf("CASCADE(%s) ERROR: %s\n", label, outcome.error.c_str());
    }
}

// The plan the structural edits are built against: the file the cascade will
// read, so uids and the placeholder uid are the writer's own.
bool load_plan_input(const std::filesystem::path& path, app::PlanInput& plan) {
    try {
        plan = app::plan_input_from_document(mpp::load_file(path.string()));
        return true;
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: cannot load %s: %s\n", path.string().c_str(), error.what());
        return false;
    }
}

app::StructuralEdit insert_edit(const char* name, std::uint32_t placeholder_uid) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::insert_task;
    edit.name = name;
    edit.uid = placeholder_uid;
    return edit;
}

app::StructuralEdit text1_edit(std::uint32_t uid, const char* text) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::set_field;
    edit.uid = uid;
    edit.field_id = 0x0b400033u;  // Text1, the measured stored field
    edit.field_value.text = text;
    return edit;
}

app::StructuralEdit delete_edit(std::uint32_t uid) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::delete_task;
    edit.uid = uid;
    return edit;
}

app::StructuralDocumentEdits document_edit_request(const app::ViewStateEdits& view,
                                                   const app::CustomFormulaEdit& formula) {
    app::StructuralDocumentEdits request;
    request.view = view;
    request.formula = formula;
    // The wave-15 operation itself: it mutates the cascade's CfbDocument and does
    // no file I/O, which is exactly the hook's contract.
    request.hook = app::apply_document_edits;
    return request;
}

const mpp::Task* task_with_uid(const mpp::Document& document, std::uint32_t uid) {
    for (const mpp::Task& task : document.tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

const mpp::CustomFieldValue* stored_field(const mpp::Task& task, std::uint32_t field_id) {
    for (const mpp::CustomFieldValue& field : task.custom_fields) {
        if (field.field_id == field_id) {
            return &field;
        }
    }
    return nullptr;
}

#endif  // APP_TEST_DATA_DIR && APP_TEST_OUT_DIR

// ---------------------------------------------------------------------------
// The pure parts
// ---------------------------------------------------------------------------

void test_default_save_name() {
    check_text(app::default_save_name("C:/projects/plan.mpp"), "plan-edited.mpp",
               "save name replaces the extension");
    check_text(app::default_save_name("/home/me/Programme 2026.MPP"), "Programme 2026-edited.mpp",
               "save name keeps spaces and the stem");
    check_text(app::default_save_name("no-extension"), "no-extension-edited.mpp",
               "save name works without an extension");
    check_text(app::default_save_name(""), "project-edited.mpp",
               "an empty path still gives a usable name");
    check_text(app::default_save_name("archive.tar.gz"), "archive.tar-edited.mpp",
               "only the last extension is replaced");
}

void test_collect_start_edits() {
    app::PlanInput loaded;
    loaded.tasks.push_back(make_task(1, from_ymd(2026, 1, 5, 8, 0)));
    loaded.tasks.push_back(make_task(2, from_ymd(2026, 1, 6, 8, 0)));

    core::view::RecordSet records = app::make_record_set(loaded);
    check(app::collect_start_edits(records, loaded).empty(), "identical records need no save");

    const core::view::FieldId start_field = app::task_field_id("Start");
    check(start_field != 0, "Task.Start resolved");

    // One changed start, written straight into the RecordSet (what commit_edit
    // does).
    core::view::DateValue value = app::date_value_from_date32(from_ymd(2026, 2, 2, 8, 0));
    records.set_field(1, start_field, core::view::Value::of_date(value));
    const std::vector<app::StartEdit> edits = app::collect_start_edits(records, loaded);
    check_size(edits.size(), 1, "one changed start is collected");
    if (!edits.empty()) {
        check(edits[0].uid == 2, "the edit carries the row's uid");
        check_date(edits[0].start, from_ymd(2026, 2, 2, 8, 0), "the edit carries the new date");
    }

    // Clearing the cell is a change with no date: collected, and reported by the
    // save rather than silently dropped.
    records.set_field(1, start_field, core::view::Value{});
    const std::vector<app::StartEdit> cleared = app::collect_start_edits(records, loaded);
    check_size(cleared.size(), 1, "a cleared start is collected");
    if (!cleared.empty()) {
        check_date(cleared[0].start, kDateNa, "a cleared start is NA");
    }

    // accept_start_edits moves the baseline, so a second save is a no-op.
    app::accept_start_edits(loaded, edits);
    check(app::collect_start_edits(records, loaded).size() == 1,
          "the cleared value still differs from the (baseline) date");
}

// ---------------------------------------------------------------------------
// The real write
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)

struct Plan {
    app::PlanInput loaded;
    core::view::RecordSet records{core::model::RecordType::Task};
    std::unique_ptr<core::sheet::SheetModel> sheet;
};

bool load_plan(const std::filesystem::path& path, Plan& plan) {
    try {
        const mpp::Document document = mpp::load_file(path.string());
        plan.loaded = app::plan_input_from_document(document);
        plan.records = app::make_record_set(plan.loaded);
        plan.sheet = std::make_unique<core::sheet::SheetModel>(plan.records,
                                                               app::default_sheet_columns());
        app::apply_edit_policy(*plan.sheet);
        return true;
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: cannot load %s: %s\n", path.string().c_str(), error.what());
        return false;
    }
}

void test_save_round_trip() {
    const std::filesystem::path source = kFixture;
    if (!std::filesystem::exists(source)) {
        std::printf("SKIP: %s is not present; the save round trip needs the committed fixture\n",
                    source.string().c_str());
        return;
    }
    const std::size_t source_bytes = bytes_on_disk(source);

    Plan plan;
    if (!load_plan(source, plan) || plan.sheet == nullptr || plan.loaded.tasks.empty()) {
        return;
    }
    check(plan.loaded.tasks.size() >= 1, "the fixture has tasks");
    const std::uint32_t uid = plan.loaded.tasks[0].uid;
    const date32 new_start = from_ymd(2026, 3, 2, 8, 0);

    // Edit the first task's Start through the sheet, as the keyboard does.
    check(plan.sheet->begin_edit(0, 4), "begin_edit on the first task's Start");
    plan.sheet->set_edit_text("2026-03-02 08:00");
    const core::sheet::EditOutcome outcome = plan.sheet->commit_edit(false);
    check(outcome.ok, "the edited date is accepted");
    check(outcome.changed, "the edited date changes the value");

    const std::vector<app::StartEdit> edits = app::collect_start_edits(plan.records, plan.loaded);
    check_size(edits.size(), 1, "exactly one start is pending");
    if (!edits.empty()) {
        check(edits[0].uid == uid, "the pending edit is the edited task");
        check_date(edits[0].start, new_start, "the pending edit is the new date");
    }

    std::error_code error;
    const std::filesystem::path target = out_path("app-document-test.mpp");

    // Refusing to overwrite the opened file is part of the contract.
    const app::SaveOutcome refused = app::save_start_edits(edits, source.string(), source.string());
    check(!refused.ok, "saving over the opened file is refused");
    check(refused.error.find("refusing") != std::string::npos, "the refusal explains itself");
    check_size(bytes_on_disk(source), source_bytes, "the source is untouched by the refusal");

    const app::SaveOutcome saved = app::save_start_edits(edits, source.string(), target.string());
    check(saved.ok, "the save reports success");
    check(saved.error.empty(), "a successful save has no error");
    check_size(saved.requested, 1, "the save was asked for one row");
    check_size(saved.rows_written, 1, "the save wrote one row");
    check_text(saved.path, target.string(), "the outcome names the target");
    check(!saved.api.empty(), "the outcome names the writer path");
    print_outcome("start", saved);
    for (const std::string& warning : saved.warnings) {
        std::printf("SAVE WARNING: %s\n", warning.c_str());
    }
    for (const std::string& note : saved.notes) {
        std::printf("SAVE NOTE: %s\n", note.c_str());
    }
    check_size(bytes_on_disk(source), source_bytes, "the source is untouched by the save");
    check(std::filesystem::exists(target), "the target file exists");

    // Re-read the written file with the reader: it must still be a .mpp, with the
    // same task count, and (through the documented writer path) the edited task's
    // start pinned by an SNET constraint.
    try {
        const mpp::Document reread = mpp::load_file(target.string());
        check_size(reread.tasks.size(), plan.loaded.tasks.size(), "the saved file has every task");
        const mpp::Task* task = nullptr;
        for (const mpp::Task& candidate : reread.tasks) {
            if (candidate.uid == uid) {
                task = &candidate;
                break;
            }
        }
        check(task != nullptr, "the edited task is in the saved file");
        if (task != nullptr) {
            // The reader's Effective Start (+104) is what Project recalculates on
            // open, so it is not required to move here; the constraint is what
            // makes it move.  W29 measured Project honouring exactly this.
#if !APP_MPPWRITE_SAVE_HEADER
            check_size(task->constraint_type, 4, "the start is pinned by a SNET constraint");
            check_size(task->constraint_date.raw, app::packed_from_date32(new_start),
                       "the constraint date is the edited start");
#else
            std::printf("NOTE: mppwrite::save_date_edits owns the encoding; "
                        "constraint %u date %s\n",
                        static_cast<unsigned>(task->constraint_type),
                        mpp::format_date(task->constraint_date).c_str());
#endif
        }
    } catch (const std::exception& reread_error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: the saved file does not load: %s\n", reread_error.what());
    }

    // A cleared start has no date to write: reported, nothing written, ok.
    Plan cleared_plan;
    if (load_plan(source, cleared_plan) && cleared_plan.sheet != nullptr) {
        check(cleared_plan.sheet->begin_edit(0, 4), "begin_edit before clearing");
        cleared_plan.sheet->set_edit_text("");
        const core::sheet::EditOutcome cleared = cleared_plan.sheet->commit_edit(false);
        check(cleared.ok, "an empty date clears the cell");
        const std::vector<app::StartEdit> cleared_edits =
            app::collect_start_edits(cleared_plan.records, cleared_plan.loaded);
        check_size(cleared_edits.size(), 1, "the cleared start is a pending change");
        const std::filesystem::path cleared_target = out_path("app-document-cleared.mpp");
        const app::SaveOutcome cleared_outcome =
            app::save_start_edits(cleared_edits, source.string(), cleared_target.string());
        check(cleared_outcome.ok, "a cleared start is not an error");
        check_size(cleared_outcome.rows_written, 0, "a cleared start writes no row");
        check(!cleared_outcome.warnings.empty(), "a cleared start is reported");
        check(!std::filesystem::exists(cleared_target), "nothing is written when there is no date");
    }

    // An edit for a uid that is not in the file fails cleanly (no output file).
    if (!edits.empty()) {
        std::vector<app::StartEdit> bogus;
        bogus.push_back(app::StartEdit{999999u, new_start});
        const std::filesystem::path bogus_target = out_path("app-document-bogus.mpp");
        const app::SaveOutcome bogus_outcome =
            app::save_start_edits(bogus, source.string(), bogus_target.string());
        check(!bogus_outcome.ok, "an unknown uid is an error");
        check(!bogus_outcome.error.empty(), "the error explains itself");
        check(!std::filesystem::exists(bogus_target), "a failed save leaves no file");
    }
}

// ---------------------------------------------------------------------------
// Wave 15 (a): 214/CTable date format
// ---------------------------------------------------------------------------

void test_save_view_state_date_format() {
    const std::filesystem::path source = kFixture;
    const std::size_t source_bytes = bytes_on_disk(source);
    const std::filesystem::path target = out_path("app-save-viewstate-date.mpp");

    const StreamMap before = streams_with_prefix(source, "214/");
    const std::vector<std::uint8_t> before_var2 =
        stream_bytes(source, "214/CTable/Var2Data");
    check(before_var2.size() > 11, "the fixture CTable Var2Data has a block header");
    if (before_var2.size() <= 11) {
        return;
    }
    check_size(before_var2[11], 0xFFu, "the fixture's date-format byte is the 0xff default");

    app::Document document(source.string());
    app::ViewStateEdits edits;
    edits.set_date_format = true;
    edits.date_format = 0x05;
    document.request_view_state_edits(edits);
    check(document.has_pending_edits(), "the view-state request is pending");
    check(document.view_state_edits().set_date_format, "the request kept the date-format flag");

    const app::SaveOutcome saved = document.save(target.string());
    print_outcome("date-format", saved);
    check(saved.ok, "a date-format save succeeds");
    check(saved.error.empty(), "the date-format save has no error");
    check(saved.view_state_written, "the save reports the view state was written");
    check(!saved.formula_written, "the date-format save writes no formula");

    const StreamMap after = streams_with_prefix(target, "214/");
    std::string why;
    check(same_stream_bytes(before, after, "214/CTable/Var2Data", why),
          ("only 214/CTable/Var2Data changed: " + why).c_str());
    check(stream_bytes(source, "214/CTable/FixedData") ==
              stream_bytes(target, "214/CTable/FixedData"),
          "214/CTable/FixedData is unchanged");
    check(stream_bytes(source, "214/CTable/FixedMeta") ==
              stream_bytes(target, "214/CTable/FixedMeta"),
          "214/CTable/FixedMeta is unchanged");
    check(stream_bytes(source, "214/CTable/VarMeta") ==
              stream_bytes(target, "214/CTable/VarMeta"),
          "214/CTable/VarMeta is unchanged");
    check(stream_bytes(source, "114/Props") == stream_bytes(target, "114/Props"),
          "the date-format save leaves 114/Props unchanged");

    const std::vector<std::uint8_t> after_var2 = stream_bytes(target, "214/CTable/Var2Data");
    check_size(after_var2.size(), before_var2.size(), "the CTable Var2Data size is unchanged");
    const std::vector<std::size_t> diffs = differing_offsets(before_var2, after_var2);
    check_size(diffs.size(), 1, "exactly one byte of CTable Var2Data changed");
    if (!diffs.empty()) {
        check_size(diffs[0], 11, "the changed byte is the payload's +7 (stream byte 11)");
    }
    if (after_var2.size() > 11) {
        check_size(after_var2[11], 0x05u, "the date-format byte is now 0x05");
    }

    // The re-opened file parses: one table, nine columns, date_format 5.
    const ParsedCTable parsed = parse_ctable_at(target);
    check(parsed.ok, ("the saved CTable still parses: " + parsed.error).c_str());
    if (parsed.ok) {
        check_size(parsed.state.tables.size(), 1, "the saved file still has one table");
        if (parsed.state.tables.size() == 1) {
            check(parsed.state.tables[0].decoded, "the saved table definition still decodes");
            check_size(parsed.state.tables[0].date_format, 0x05u,
                       "the saved table's date_format is 5");
            check_size(parsed.state.tables[0].column_count(), 9, "the column count is unchanged");
        }
    }
    check_size(bytes_on_disk(source), source_bytes, "the source fixture is untouched");
}

// ---------------------------------------------------------------------------
// Wave 15 (b): a 115-byte column record replacement
// ---------------------------------------------------------------------------

void test_save_view_state_column() {
    const std::filesystem::path source = kFixture;
    const std::size_t source_bytes = bytes_on_disk(source);
    const std::filesystem::path target = out_path("app-save-viewstate-column.mpp");

    const std::vector<std::uint8_t> before_var2 =
        stream_bytes(source, "214/CTable/Var2Data");
    const StreamMap before = streams_with_prefix(source, "214/");

    app::Document document(source.string());
    app::ViewStateEdits edits;
    edits.replace_column = true;
    edits.column_index = 1;              // an existing column
    edits.field_id = 0x0b400033u;        // Text1
    edits.title = "Text1";
    document.request_view_state_edits(edits);

    const app::SaveOutcome saved = document.save(target.string());
    print_outcome("column", saved);
    check(saved.ok, "a column-replacement save succeeds");
    check(saved.error.empty(), "the column-replacement save has no error");
    check(saved.view_state_written, "the save reports the view state was written");

    const StreamMap after = streams_with_prefix(target, "214/");
    std::string why;
    check(same_stream_bytes(before, after, "214/CTable/Var2Data", why),
          ("only 214/CTable/Var2Data changed: " + why).c_str());

    const std::vector<std::uint8_t> after_var2 = stream_bytes(target, "214/CTable/Var2Data");
    check_size(after_var2.size(), before_var2.size(), "the CTable Var2Data size is unchanged");
    check(stream_bytes(source, "114/Props") == stream_bytes(target, "114/Props"),
          "the column save leaves 114/Props unchanged");
    const std::vector<std::size_t> diffs = differing_offsets(before_var2, after_var2);
    // The single block starts at Var2Data offset 0: 4-byte length word + 12-byte
    // block header + index 1 * 115-byte record = bytes 131..245.
    bool inside_record = !diffs.empty();
    for (const std::size_t offset : diffs) {
        if (offset < 131u || offset >= 246u) {
            inside_record = false;
        }
    }
    check(inside_record, "every changed byte is inside the index-1 115-byte record");
    check(!diffs.empty(), "the replacement changed at least one byte");

    const ParsedCTable parsed = parse_ctable_at(target);
    check(parsed.ok, ("the saved CTable still parses: " + parsed.error).c_str());
    if (parsed.ok && parsed.state.tables.size() == 1) {
        const msproject::viewstate::TableDef& table = parsed.state.tables[0];
        check_size(table.column_count(), 9, "the column count is unchanged");
        if (table.column_count() > 1) {
            check_size(table.columns[1].field_id, 0x0b400033u,
                       "the replacement's field id is Text1");
            check_text(table.columns[1].title, "Text1", "the replacement's title is Text1");
        }
        check_size(table.last_column_index, 8, "the last-column index is unchanged");
    } else {
        check(false, "the saved file has one table");
    }
    check_size(bytes_on_disk(source), source_bytes, "the source fixture is untouched");
}

// ---------------------------------------------------------------------------
// Wave 15 (c): a save with no pending edit leaves 214/ (and 114/Props) alone
// ---------------------------------------------------------------------------

void test_save_without_edits() {
    const std::filesystem::path source = kFixture;
    const std::size_t source_bytes = bytes_on_disk(source);
    const std::filesystem::path target = out_path("app-save-no-edits.mpp");

    const StreamMap before_214 = streams_with_prefix(source, "214/");
    const std::vector<std::uint8_t> before_props = stream_bytes(source, "114/Props");

    app::Document document(source.string());
    check(!document.has_pending_edits(), "a fresh Document has no pending edits");
    const app::SaveOutcome saved = document.save(target.string());
    print_outcome("no-edits", saved);
    check(saved.ok, "a save with nothing pending succeeds");
    check(saved.error.empty(), "a save with nothing pending has no error");
    check(!saved.view_state_written, "no view state is written");
    check(!saved.formula_written, "no formula is written");
    check(!saved.custom_field_flag_restored, "the custom-field flag is not touched");
    check_size(saved.requested, 0, "nothing was requested");

    const StreamMap after_214 = streams_with_prefix(target, "214/");
    std::string why;
    check(same_stream_bytes(before_214, after_214, "", why),
          ("every 214/ stream is byte-identical: " + why).c_str());
    check(stream_bytes(target, "114/Props") == before_props,
          "114/Props is byte-identical with nothing pending");
    check_size(bytes_on_disk(target), source_bytes, "the untouched save has the source's size");
    check(file_bytes(target) == file_bytes(source),
          "a save with nothing pending writes the source container byte-identically");
    check_size(bytes_on_disk(source), source_bytes, "the source fixture is untouched");
}

// ---------------------------------------------------------------------------
// Wave 15 (d): setting a custom-field formula
// ---------------------------------------------------------------------------

void test_save_formula_set() {
    const std::filesystem::path source = kFixture;
    const std::filesystem::path target = out_path("app-save-formula.mpp");
    const StreamMap before_214 = streams_with_prefix(source, "214/");

    app::Document document(source.string());
    std::string request_error;
    check(document.apply_custom_formula(0x0b400036u, "[Name]", &request_error),
          "a Text2 formula request is accepted");
    check(request_error.empty(), "the accepted request has no error");
    check(document.has_pending_edits(), "the formula request is pending");
    check(!document.view_state_edits().any(), "the formula request is not a view-state edit");

    const app::SaveOutcome saved = document.save(target.string());
    print_outcome("formula-set", saved);
    check(saved.ok, "a formula save succeeds");
    check(saved.error.empty(), "the formula save has no error");
    check(saved.formula_written, "the save reports the formula was written");
    check(!saved.view_state_written, "the formula save writes no view state");
    check(!saved.custom_field_flag_restored, "a set already leaves the flag at 1");

    const std::vector<std::uint8_t> props = stream_bytes(target, "114/Props");
    std::uint16_t flag = 0;
    check(props_flag(props, flag), "the saved 114/Props has the 0x02401392 flag record");
    check_size(flag, 1, "the custom-field data flag is 1 after setting a formula");

    msproject::props::PropsDocument parsed;
    msproject::props::ParseError parse_error;
    check(msproject::props::parse_props(props, parsed, &parse_error),
          "the saved 114/Props parses");
    const std::vector<msproject::props::FormulaBlock> formulas =
        msproject::props::extract_formulas(parsed);
    check_size(formulas.size(), 1, "one formula reads back");
    if (!formulas.empty()) {
        check_size(formulas[0].field_id, 0x0b400036u, "the formula is on Text2");
        check_text(formulas[0].formula, "[MSPJ188743694]",
                   "the formula stores [Name] as its measured MSPJ token");
        check_text(msproject::props::formula_with_field_ids(formulas[0].formula),
                   "[0x0b40000e]", "the MSPJ token decodes to Task.Name's field id");
    }

    const StreamMap after_214 = streams_with_prefix(target, "214/");
    std::string why;
    check(same_stream_bytes(before_214, after_214, "", why),
          ("a formula save leaves every 214/ stream unchanged: " + why).c_str());
}

// ---------------------------------------------------------------------------
// Wave 15 (e): clearing formulas while a custom-field VALUE remains
// ---------------------------------------------------------------------------

void test_save_formula_clear_with_values() {
    const std::filesystem::path source = kFixture;
    const std::filesystem::path formula_path = out_path("app-save-formula-for-clear.mpp");
    const std::filesystem::path valued_path = out_path("app-save-valued.mpp");
    const std::filesystem::path cleared_path = out_path("app-save-cleared.mpp");

    // 1. A formula, so "clear" has something to remove.
    app::Document formula_document(source.string());
    check(formula_document.apply_custom_formula(0x0b400036u, "[Name]", nullptr),
          "the setup formula is queued");
    const app::SaveOutcome formula_saved = formula_document.save(formula_path.string());
    check(formula_saved.ok, "the setup formula save succeeds");

    // 2. A Text1 value on uid 1, through the existing field-value writer (this is
    //    the storage the flag is really about).
    bool value_written = false;
    try {
        mppwrite::CfbDocument valued = mppwrite::CfbDocument::from_file(formula_path.string());
        mppwrite::FieldEdit edit;
        edit.uid = 1;
        edit.value.field_id = 0x0b400033u;
        edit.value.kind = mppwrite::FieldKind::text;
        edit.value.text = "alpha";
        const mppwrite::FieldWriteResult result =
            mppwrite::save_field_values(valued, {edit}, valued_path.string());
        value_written = result.fields_written == 1;
    } catch (const std::exception& error) {
        std::printf("FAIL: cannot write the Text1 value: %s\n", error.what());
    }
    check(value_written, "the Text1 value was written");

    // 3. Clear every formula.  core/props writes the flag 0; the app must put it
    //    back to 1 because the Text1 value is still stored.
    app::Document clear_document(valued_path.string());
    clear_document.clear_custom_formula();
    const app::SaveOutcome cleared = clear_document.save(cleared_path.string());
    print_outcome("formula-clear-values", cleared);
    check(cleared.ok, "clearing formulas on a value-bearing file succeeds");
    check(cleared.error.empty(), "the clear has no error");
    check(cleared.formula_written, "the clear rewrote 114/Props");
    check(cleared.custom_field_flag_restored,
          "the app reports the custom-field flag was restored to 1");

    const std::vector<std::uint8_t> props = stream_bytes(cleared_path, "114/Props");
    std::uint16_t flag = 0;
    check(props_flag(props, flag), "the cleared 114/Props has the flag record");
    check_size(flag, 1, "the flag stays 1 because a custom-field value remains");

    msproject::props::PropsDocument parsed;
    msproject::props::ParseError parse_error;
    check(msproject::props::parse_props(props, parsed, &parse_error),
          "the cleared 114/Props parses");
    check_size(msproject::props::extract_formulas(parsed).size(), 0,
               "the cleared 114/Props has no formulas");

    // 4. The value itself is still readable through the reader.
    try {
        const mpp::Document reread = mpp::load_file(cleared_path.string());
        const mpp::CustomFieldValue* value = nullptr;
        for (const mpp::Task& task : reread.tasks) {
            if (task.uid != 1) {
                continue;
            }
            for (const mpp::CustomFieldValue& field : task.custom_fields) {
                if (field.field_id == 0x0b400033u) {
                    value = &field;
                }
            }
        }
        check(value != nullptr, "the Text1 value is still stored after the clear");
        if (value != nullptr) {
            check(value->present, "the Text1 value is present");
            check_text(value->text, "alpha", "the Text1 value is still readable");
        }
    } catch (const std::exception& error) {
        check(false, ("the cleared file does not load: " + std::string(error.what())).c_str());
    }
}

// ---------------------------------------------------------------------------
// Wave 15 (f): clearing on a value-free project, and a refused edit
// ---------------------------------------------------------------------------

void test_save_formula_clear_value_free() {
    const std::filesystem::path source = kFixture;
    const std::filesystem::path target = out_path("app-save-clear-value-free.mpp");
    const std::vector<std::uint8_t> before_props = stream_bytes(source, "114/Props");

    app::Document document(source.string());
    document.clear_custom_formula();
    const app::SaveOutcome saved = document.save(target.string());
    print_outcome("formula-clear-value-free", saved);
    check(saved.ok, "a clear on a value-free project succeeds");
    check(saved.error.empty(), "the value-free clear has no error");
    check(!saved.formula_written, "an empty formula table is not rewritten");
    check(!saved.custom_field_flag_restored, "the flag is not restored without values");

    const std::vector<std::uint8_t> props = stream_bytes(target, "114/Props");
    check(props == before_props, "the value-free clear leaves 114/Props byte-identical");
    std::uint16_t flag = 1;
    check(props_flag(props, flag), "the value-free 114/Props has the flag record");
    check_size(flag, 0, "the custom-field data flag is 0 on a value-free project");
}

void test_save_refused_view_state() {
    const std::filesystem::path source = kFixture;
    const std::filesystem::path target = out_path("app-save-refused-viewstate.mpp");
    const StreamMap before_214 = streams_with_prefix(source, "214/");

    app::Document document(source.string());
    app::ViewStateEdits edits;
    edits.replace_column = true;
    edits.column_index = 99;         // out of range
    edits.field_id = 0x0b400033u;
    edits.title = "Text1";
    document.request_view_state_edits(edits);

    const app::SaveOutcome saved = document.save(target.string());
    print_outcome("refused-viewstate", saved);
    check(saved.ok, "a refused view-state edit does not fail the save");
    check(saved.error.empty(), "a refused view-state edit is a warning, not an error");
    check(!saved.view_state_written, "the refused edit writes no view state");
    check(std::filesystem::exists(target), "the file is still written");
    bool warning_named_it = false;
    for (const std::string& warning : saved.warnings) {
        if (warning.find("view state not written") != std::string::npos) {
            warning_named_it = true;
        }
    }
    check(warning_named_it, "a warning explains the refused view-state edit");

    const StreamMap after_214 = streams_with_prefix(target, "214/");
    std::string why;
    check(same_stream_bytes(before_214, after_214, "", why),
          ("a refused edit leaves every 214/ stream unchanged: " + why).c_str());
}

// ---------------------------------------------------------------------------
// Wave 15: request validation and a combined save
// ---------------------------------------------------------------------------

void test_document_request_validation() {
    app::Document document("unused.mpp");
    check(!document.has_pending_edits(), "a fresh Document has nothing pending");

    std::string error;
    check(!document.apply_custom_formula(0u, "[Name]", &error),
          "a field id of 0 is refused");
    check(!error.empty(), "the field-id refusal explains itself");
    check(!document.has_pending_edits(), "a refused request leaves nothing pending");

    error.clear();
    check(!document.apply_custom_formula(0x0b400036u, "", &error),
          "an empty formula is refused");
    check(!error.empty(), "the empty-formula refusal explains itself");
    check(!document.has_pending_edits(), "a refused formula leaves nothing pending");

    check(document.apply_custom_formula(0x0b400036u, "[Name]", &error),
          "a valid formula is accepted");
    check(document.has_pending_edits(), "the valid formula is pending");
    document.discard_custom_formula();
    check(!document.has_pending_edits(), "discarding clears the formula request");
}

void test_save_start_date_with_view_and_formula() {
    const std::filesystem::path source = kFixture;
    const std::filesystem::path target = out_path("app-save-pipeline.mpp");
    const date32 new_start = from_ymd(2026, 3, 2, 8, 0);

    std::uint32_t uid = 0;
    try {
        const mpp::Document loaded = mpp::load_file(source.string());
        if (!loaded.tasks.empty()) {
            uid = loaded.tasks[0].uid;
        }
    } catch (const std::exception& error) {
        check(false, ("cannot load the fixture for the pipeline test: " +
                      std::string(error.what())).c_str());
        return;
    }
    check(uid != 0, "the pipeline test has a task uid");
    if (uid == 0) {
        return;
    }

    app::Document document(source.string());
    app::ViewStateEdits edits;
    edits.set_date_format = true;
    edits.date_format = 0x05;
    document.request_view_state_edits(edits);
    check(document.apply_custom_formula(0x0b400036u, "[Name]", nullptr),
          "the pipeline test queues its formula");
    const std::vector<app::StartEdit> start_edits{{uid, new_start}};

    const app::SaveOutcome saved = document.save(start_edits, target.string());
    print_outcome("pipeline", saved);
    check(saved.ok, "the one-write pipeline save succeeds");
    check(saved.error.empty(), "the pipeline save has no error");
    check_size(saved.requested, 3, "the pipeline save records three requests");
    check_size(saved.rows_written, 1, "the pipeline save writes the start pin");
    check(saved.view_state_written, "the pipeline save writes the view state");
    check(saved.formula_written, "the pipeline save writes the formula");

    try {
        const mpp::Document reread = mpp::load_file(target.string());
        const mpp::Task* task = nullptr;
        for (const mpp::Task& candidate : reread.tasks) {
            if (candidate.uid == uid) {
                task = &candidate;
            }
        }
        check(task != nullptr, "the pipeline file still holds the task");
        if (task != nullptr) {
#if !APP_MPPWRITE_SAVE_HEADER
            check_size(task->constraint_type, 4, "the pipeline file has the SNET pin");
            check_size(task->constraint_date.raw, app::packed_from_date32(new_start),
                       "the pipeline pin carries the edited date");
#else
            std::printf("NOTE: the pipeline save's date encoding is "
                        "mppwrite::save_date_edits' (constraint %u date %s); its own suite "
                        "proves the SNET words\n",
                        static_cast<unsigned>(task->constraint_type),
                        mpp::format_date(task->constraint_date).c_str());
#endif
        }
    } catch (const std::exception& error) {
        check(false, ("the pipeline file does not load: " + std::string(error.what())).c_str());
    }

    const ParsedCTable parsed = parse_ctable_at(target);
    check(parsed.ok, ("the pipeline CTable parses: " + parsed.error).c_str());
    if (parsed.ok && parsed.state.tables.size() == 1) {
        check_size(parsed.state.tables[0].date_format, 0x05u,
                   "the pipeline date format is 5");
    }
    std::uint16_t flag = 0;
    check(props_flag(stream_bytes(target, "114/Props"), flag),
          "the pipeline file has the flag record");
    check_size(flag, 1, "the pipeline file leaves the custom-field flag at 1");
    check(stream_bytes(source, "214/CTable/Var2Data") !=
              stream_bytes(target, "214/CTable/Var2Data"),
          "the pipeline save actually changed the table state");
}

void test_save_formula_and_view_state() {
    const std::filesystem::path source = kFixture;
    const std::filesystem::path target = out_path("app-save-combined.mpp");

    app::Document document(source.string());
    app::ViewStateEdits edits;
    edits.set_date_format = true;
    edits.date_format = 0x05;
    document.request_view_state_edits(edits);
    check(document.apply_custom_formula(0x0b400036u, "[Name]", nullptr),
          "the combined save queues its formula");

    const app::SaveOutcome saved = document.save(target.string());
    print_outcome("combined", saved);
    check(saved.ok, "a combined formula + view-state save succeeds");
    check(saved.formula_written, "the combined save writes the formula");
    check(saved.view_state_written, "the combined save writes the view state");
    check_size(saved.requested, 2, "the combined save records two requests");

    const ParsedCTable parsed = parse_ctable_at(target);
    check(parsed.ok, ("the combined save's CTable parses: " + parsed.error).c_str());
    if (parsed.ok && parsed.state.tables.size() == 1) {
        check_size(parsed.state.tables[0].date_format, 0x05u,
                   "the combined save's date format is 5");
    }
    std::uint16_t flag = 0;
    check(props_flag(stream_bytes(target, "114/Props"), flag),
          "the combined save has the flag record");
    check_size(flag, 1, "the combined save leaves the custom-field flag at 1");
}

// ---------------------------------------------------------------------------
// Wave 17 (a): no hook => the cascade is byte-identical to today's output
// ---------------------------------------------------------------------------
//
// Three ways of asking for the same thing must produce the same bytes:
//   1. the pre-wave-17 signature (five arguments, no request at all);
//   2. the new signature with an EMPTY StructuralDocumentEdits request;
//   3. the documented writer sequence applied by hand -- append the row, write the
//      value, then the one date write -- which is what the cascade does.
// The same three runs, and app_edits_fields_tests' own combined file, are all
// SHA-256 52ECE9E1E4BF6F621AE621AA913F94433B71CE7014B0C13D31D6BC9903A4FB01 on
// the committed fixture; the pre-change binary (build\a1-app, built before this
// stream's edit) produces exactly that file, so "byte-identical to today's
// cascade output" is a checked fact, not a hope (docs/handoff/b1.md section 5).
void test_cascade_without_hook_is_unchanged() {
    const std::filesystem::path source = kFixture;
    const std::size_t source_bytes = bytes_on_disk(source);
    const std::filesystem::path target = out_path("app-cascade-no-hook.mpp");
    const std::filesystem::path again = out_path("app-cascade-empty-request.mpp");
    const std::filesystem::path reference = out_path("app-cascade-reference.mpp");
    const std::filesystem::path scratch = out_path("app-cascade-reference-tmp.mpp");
    const date32 new_start = from_ymd(2026, 3, 2, 8, 0);

    app::PlanInput plan;
    if (!load_plan_input(source, plan) || plan.tasks.empty()) {
        return;
    }
    const std::uint32_t uid = plan.tasks.front().uid;

    // The edit set app_edits_fields_tests already runs: one Text1 value, one
    // appended task, one Start pin.
    app::StructuralEdits edits;
    check(edits.push(text1_edit(uid, "combined")), "the no-hook field edit is queued");
    check(edits.push(insert_edit("Field Task", edits.next_placeholder_uid(plan))),
          "the no-hook insert is queued");
    check_size(edits.size(), 2, "two structural edits are pending");
    const std::vector<app::StartEdit> starts{{uid, new_start}};

    // (1) Today's call shape.
    const app::StructuralSaveResult plain =
        app::save_structural_edits(edits, starts, source.string(), target.string(), false);
    print_cascade("no-hook", plain);
    check(plain.ok, "the no-hook cascade still saves");
    check_size(plain.added, 1, "the no-hook cascade added the row");
    check_size(plain.fields_written, 1, "the no-hook cascade wrote the value");
    check_size(plain.start_rows_written, 1, "the no-hook cascade wrote the start pin");
    check(!plain.document_edits_requested, "no document edits were requested");
    check(!plain.document_edits_written, "no document-edit hook ran");
    check(plain.api.find("apply_document_edits") == std::string::npos,
          "the no-hook api does not name the document-edit hook");
    check(std::filesystem::exists(target), "the no-hook cascade wrote its target");
    check(!std::filesystem::exists(target.string() + ".w51-structural-tmp"),
          "the no-hook cascade left no intermediate file");

    // (2) The new parameter with an empty request: same path, same bytes.
    const app::StructuralSaveResult empty_request = app::save_structural_edits(
        edits, starts, source.string(), again.string(), false, app::StructuralDocumentEdits{});
    print_cascade("empty-request", empty_request);
    check(empty_request.ok, "an empty document-edit request still saves");
    check(!empty_request.document_edits_written, "an empty request runs no hook");
    check(file_bytes(again) == file_bytes(target),
          "an empty request is byte-identical to the pre-wave-17 call");

    // (3) The writer sequence applied by hand, independent of edit_model.cpp.
    bool reference_ok = false;
    try {
        mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(source.string());
        mppwrite::NewTaskRow row;
        row.name = "Field Task";  // the writer allocates uid/id (0 = allocate)
        row.uid = 0;
        mppwrite::add_task_row(document, row, scratch.string());
        mppwrite::FieldEdit field;
        field.uid = uid;
        field.value.field_id = 0x0b400033u;
        field.value.kind = mppwrite::FieldKind::text;
        field.value.text = "combined";
        mppwrite::save_field_values(document, std::vector<mppwrite::FieldEdit>{field},
                                    scratch.string());
#if APP_MPPWRITE_SAVE_HEADER
        mppwrite::DateEdit date;
        date.uid = uid;
        date.start = app::packed_from_date32(new_start);
        mppwrite::save_date_edits(document, std::vector<mppwrite::DateEdit>{date},
                                  reference.string());
#else
        const mppwrite::TaskTable table = mppwrite::TaskTable::from_document(document);
        mppwrite::pin_task_start(document, table.row_for_uid(uid),
                                 app::packed_from_date32(new_start));
        document.save(reference.string());
#endif
        reference_ok = true;
    } catch (const std::exception& error) {
        check(false, ("the hand-built reference cascade failed: " + std::string(error.what()))
                         .c_str());
    }
    check(reference_ok, "the hand-built reference cascade ran");
    if (reference_ok) {
        check(file_bytes(reference) == file_bytes(target),
              "the no-hook cascade is byte-identical to the writer sequence applied by hand");
    }

    check_size(bytes_on_disk(source), source_bytes, "the source fixture is untouched");
    std::error_code error;
    std::filesystem::remove(scratch, error);
}

// ---------------------------------------------------------------------------
// Wave 17 (b): structural add + field value + formula + view state, ONE save
// ---------------------------------------------------------------------------
void test_cascade_with_document_edits_in_one_write() {
    const std::filesystem::path source = kFixture;
    const std::size_t source_bytes = bytes_on_disk(source);
    const std::filesystem::path prepped = out_path("app-cascade-prepped.mpp");
    const std::filesystem::path target = out_path("app-cascade-one-write.mpp");
    const std::filesystem::path control = out_path("app-cascade-no-value-control.mpp");
    const std::filesystem::path with_start = out_path("app-cascade-one-write-start.mpp");
    const std::filesystem::path via_document = out_path("app-cascade-via-document.mpp");

    if (!std::filesystem::exists(source)) {
        return;
    }

    // A source file that already stores a FORMULA (and no custom-field value), so
    // the clear below has something to remove and the flag decision is real.
    {
        app::Document setup(source.string());
        check(setup.apply_custom_formula(0x0b400036u, "[Name]", nullptr),
              "the cascade setup queues a Text2 formula");
        const app::SaveOutcome saved = setup.save(prepped.string());
        print_outcome("cascade-setup", saved);
        check(saved.ok, "the cascade setup formula is written");
        check(saved.formula_written, "the cascade setup reports the formula");
    }
    const std::size_t prepped_bytes = bytes_on_disk(prepped);

    app::PlanInput plan;
    if (!load_plan_input(prepped, plan) || plan.tasks.empty()) {
        return;
    }
    const std::uint32_t uid = plan.tasks.front().uid;

    app::StructuralEdits edits;
    check(edits.push(text1_edit(uid, "alpha")), "the one-write field edit is queued");
    check(edits.push(insert_edit("Hook Task", edits.next_placeholder_uid(plan))),
          "the one-write insert is queued");

    app::ViewStateEdits view;
    view.set_date_format = true;
    view.date_format = 0x05;
    app::CustomFormulaEdit formula;
    formula.operation = app::CustomFormulaEdit::Operation::clear;
    app::StructuralDocumentEdits request = document_edit_request(view, formula);
    check(request.any(), "the one-write request carries pending document edits");

    const app::StructuralSaveResult outcome = app::save_structural_edits(
        edits, {}, prepped.string(), target.string(), false, request);
    print_cascade("one-write", outcome);
    check(outcome.ok, "structural + field + view-state + formula saves in one write");
    if (!outcome.ok) {
        return;
    }
    check_size(outcome.added, 1, "the one-write save added the task");
    check_size(outcome.fields_written, 1, "the one-write save wrote the field value");
    check(outcome.document_edits_requested, "the save reports document edits were requested");
    check(outcome.document_edits_written, "the save reports the document-edit hook ran");
    check(outcome.document_edits.view_state_written, "the view state was written");
    check(outcome.document_edits.formula_written, "the formula clear was written");
    check(outcome.document_edits.custom_field_flag_restored,
          "the 0x02401392 decision saw the value written in the SAME save");
    check(outcome.api.find("apply_document_edits") != std::string::npos,
          "the outcome names apply_document_edits");
    check(outcome.api.find("CfbDocument::save") != std::string::npos,
          "the only target write was the explicit container save");
    bool flag_warning = false;
    for (const std::string& warning : outcome.warnings) {
        if (warning.find("0x02401392") != std::string::npos) {
            flag_warning = true;
        }
    }
    check(flag_warning, "a warning explains the flag was kept at 1");
    check(!std::filesystem::exists(target.string() + ".w51-structural-tmp"),
          "the one-write save left no intermediate file");
    check_size(bytes_on_disk(prepped), prepped_bytes, "the prepared source is untouched");

    // Reopen: all four edits are in that one file.
    try {
        const mpp::Document reread = mpp::load_file(target.string());
        check_size(reread.tasks.size(), plan.tasks.size() + 1,
                   "the reopened file has the added task");
        const mpp::Task* task = task_with_uid(reread, uid);
        check(task != nullptr, "the field-edited task is in the reopened file");
        if (task != nullptr) {
            const mpp::CustomFieldValue* value = stored_field(*task, 0x0b400033u);
            check(value != nullptr && value->present, "the Text1 value is stored");
            if (value != nullptr) {
                check_text(value->text, "alpha", "the Text1 value is alpha");
            }
        }
    } catch (const std::exception& error) {
        check(false, ("the one-write file does not load: " + std::string(error.what())).c_str());
    }

    const std::vector<std::uint8_t> props = stream_bytes(target, "114/Props");
    msproject::props::PropsDocument parsed;
    msproject::props::ParseError parse_error;
    check(msproject::props::parse_props(props, parsed, &parse_error),
          "the one-write file's 114/Props parses");
    check_size(msproject::props::extract_formulas(parsed).size(), 0,
               "the formula clear landed in the same write");
    std::uint16_t flag = 0;
    check(props_flag(props, flag), "the one-write file has the flag record");
    check_size(flag, 1, "the flag is 1 because a value was written in the same save");

    const ParsedCTable table = parse_ctable_at(target);
    check(table.ok, ("the one-write CTable parses: " + table.error).c_str());
    if (table.ok && table.state.tables.size() == 1) {
        check_size(table.state.tables[0].date_format, 0x05u,
                   "the one-write file's date format is 5");
    }
    check(stream_bytes(prepped, "214/CTable/Var2Data") !=
              stream_bytes(target, "214/CTable/Var2Data"),
          "the view-state edit changed the table state");

    // The control: the same two document edits with NO field value written. The
    // flag then has nothing to protect, so the clear leaves it 0 -- which proves
    // the 1 above came from the value the field writer had just written, i.e. that
    // the hook runs AFTER the field-value writers.
    app::StructuralEdits control_edits;
    check(control_edits.push(insert_edit("Hook Task", control_edits.next_placeholder_uid(plan))),
          "the control insert is queued");
    const app::StructuralSaveResult control_outcome = app::save_structural_edits(
        control_edits, {}, prepped.string(), control.string(), false,
        document_edit_request(view, formula));
    print_cascade("no-value-control", control_outcome);
    check(control_outcome.ok, "the no-value control saves");
    check(!control_outcome.document_edits.custom_field_flag_restored,
          "without a value in the same save the flag is not kept at 1");
    std::uint16_t control_flag = 1;
    check(props_flag(stream_bytes(control, "114/Props"), control_flag),
          "the control file has the flag record");
    check_size(control_flag, 0, "the control file's flag is 0");

    // The Document-level wiring: `Document::save_structural()` is the shell's call
    // shape for exactly this save, so it must be the same one-write path.
    {
        app::Document document(prepped.string());
        document.request_view_state_edits(view);
        document.clear_custom_formula();
        check(document.has_pending_edits(), "the Document holds the pending edits");
        const app::StructuralSaveResult via_doc =
            document.save_structural(edits, {}, via_document.string(), false);
        print_cascade("via-document", via_doc);
        check(via_doc.ok, "Document::save_structural saves");
        check(via_doc.document_edits_written, "Document::save_structural ran the hook");
        check(file_bytes(via_document) == file_bytes(target),
              "Document::save_structural is byte-identical to the free-function call");
    }

    // A Start pin riding along: the date writer is then the single target write,
    // AFTER the hook (the other half of the routing rule).
    {
        const std::vector<app::StartEdit> starts{{uid, from_ymd(2026, 3, 2, 8, 0)}};
        const app::StructuralSaveResult pinned = app::save_structural_edits(
            edits, starts, prepped.string(), with_start.string(), false, request);
        print_cascade("one-write-with-start", pinned);
        check(pinned.ok, "structural + field + view-state + formula + start saves in one write");
        check_size(pinned.start_rows_written, 1, "the start pin was written");
        check(pinned.document_edits_written, "the hook ran before the date writer");
        check(pinned.api.find("save_date_edits") != std::string::npos,
              "the final write was the date writer");
        check(pinned.api.find("CfbDocument::save") == std::string::npos,
              "no separate container save was needed");
        check(file_bytes(with_start) != file_bytes(target), "the start pin changed the file");
        try {
            const mpp::Document reread = mpp::load_file(with_start.string());
            const mpp::Task* task = task_with_uid(reread, uid);
            check(task != nullptr, "the pinned task reopens");
            if (task != nullptr) {
                check_size(task->constraint_type, 4, "the start edit is a SNET pin");
                check(task->constraint_date.raw ==
                          app::packed_from_date32(from_ymd(2026, 3, 2, 8, 0)),
                      "the pin carries the edited date");
            }
        } catch (const std::exception& error) {
            check(false, ("the one-write-with-start file does not load: " +
                          std::string(error.what())).c_str());
        }
        const ParsedCTable pinned_table = parse_ctable_at(with_start);
        check(pinned_table.ok && pinned_table.state.tables.size() == 1 &&
                  pinned_table.state.tables[0].date_format == 0x05u,
              "the pinned file also carries the date format");
        std::uint16_t pinned_flag = 0;
        check(props_flag(stream_bytes(with_start, "114/Props"), pinned_flag),
              "the pinned file has the flag record");
        check_size(pinned_flag, 1, "the pinned file keeps the flag at 1");
    }

    // What A1's second-write fallback would do if it were left in place: re-open
    // the file the one write produced, with the same pending edits, and save
    // again.  Every edit is already there, so the write is a NO-OP -- nothing is
    // replaced and the container comes back byte-identical.  It can only cost a
    // write, never make a second change; the shell should still skip it
    // (docs/handoff/b1.md section 4).
    {
        const std::vector<std::uint8_t> before = file_bytes(target);
        app::Document again_document(target.string());
        again_document.request_view_state_edits(view);
        again_document.clear_custom_formula();
        app::SaveOptions options;
        options.allow_source_overwrite = true;
        const app::SaveOutcome second = again_document.save(target.string(), options);
        print_outcome("fallback-would-be", second);
        check(second.ok, "the second write a fallback would do succeeds");
        check(!second.view_state_written, "the fallback re-applies no view state");
        check(!second.formula_written, "the fallback re-applies no formula");
        check(file_bytes(target) == before, "the fallback's second write is byte-identical");
    }

    check_size(bytes_on_disk(source), source_bytes, "the source fixture is untouched");
}

// ---------------------------------------------------------------------------
// Wave 17 (c): a hook refusal fails the save cleanly
// ---------------------------------------------------------------------------
void test_cascade_hook_refusal() {
    const std::filesystem::path source = kFixture;
    const std::size_t source_bytes = bytes_on_disk(source);
    const std::filesystem::path target = out_path("app-cascade-hook-refused.mpp");
    const std::filesystem::path guarded = out_path("app-cascade-hook-guarded.mpp");
    const std::filesystem::path unhooked = out_path("app-cascade-unhooked.mpp");

    app::PlanInput plan;
    if (!load_plan_input(source, plan) || plan.tasks.empty()) {
        return;
    }
    app::StructuralEdits edits;
    check(edits.push(insert_edit("Refused Task", edits.next_placeholder_uid(plan))),
          "the refusal test insert is queued");

    app::ViewStateEdits view;
    view.set_date_format = true;
    view.date_format = 0x05;
    app::StructuralDocumentEdits request;
    request.view = view;
    request.hook = [](mppwrite::CfbDocument&, const app::ViewStateEdits&,
                      const app::CustomFormulaEdit&) {
        app::DocumentEditOutcome outcome;
        outcome.ok = false;
        outcome.error = "formula not written: refused by the test hook";
        return outcome;
    };

    // A start pin rides along, so the date writer WOULD have been the final write:
    // the refusal must stop it before it reaches the target.
    const std::vector<app::StartEdit> starts{{plan.tasks.front().uid, from_ymd(2026, 3, 2, 8, 0)}};
    const app::StructuralSaveResult refused =
        app::save_structural_edits(edits, starts, source.string(), target.string(), false, request);
    print_cascade("hook-refused", refused);
    check(!refused.ok, "a refused hook fails the save");
    check(refused.error.find("refused by the test hook") != std::string::npos,
          "the save reports the hook's own error");
    check(!refused.document_edits_written, "a refused hook reports no document write");
    check(!std::filesystem::exists(target), "a refused hook leaves no target file");
    check(!std::filesystem::exists(target.string() + ".w51-structural-tmp"),
          "a refused hook removes the intermediate file");
    check_size(bytes_on_disk(source), source_bytes, "the source is untouched by the refusal");

    // A pre-existing target is left byte-identical: the refusal happens before the
    // final write, not before the writer calls.
    std::error_code error;
    std::filesystem::copy_file(source, guarded,
                               std::filesystem::copy_options::overwrite_existing, error);
    const std::size_t guarded_bytes = bytes_on_disk(guarded);
    const app::StructuralSaveResult guarded_outcome = app::save_structural_edits(
        edits, starts, source.string(), guarded.string(), false, request);
    check(!guarded_outcome.ok, "the hook refusal fires with a pre-existing target");
    check_size(bytes_on_disk(guarded), guarded_bytes,
               "the pre-existing target is byte-identical after the refusal");
    check(file_bytes(guarded) == file_bytes(source),
          "the pre-existing target still holds the source's bytes");

    // Pending document edits WITHOUT a hook are refused, never dropped silently.
    app::StructuralDocumentEdits unbooked;
    unbooked.view = view;
    const app::StructuralSaveResult dropped = app::save_structural_edits(
        edits, {}, source.string(), unhooked.string(), false, unbooked);
    print_cascade("unhooked", dropped);
    check(!dropped.ok, "document edits without a hook are refused");
    check(dropped.error.find("hook") != std::string::npos,
          "the refusal names the missing hook");
    check(!std::filesystem::exists(unhooked), "the refused request writes no target");
    check_size(bytes_on_disk(source), source_bytes, "the source is untouched by every refusal");
}

// ---------------------------------------------------------------------------
// Wave 17 (d): a task delete + a formula change in ONE save
// ---------------------------------------------------------------------------
void test_cascade_delete_with_formula() {
    const std::filesystem::path source = kFixture;
    const std::size_t source_bytes = bytes_on_disk(source);
    const std::filesystem::path target = out_path("app-cascade-delete-formula.mpp");

    app::PlanInput plan;
    if (!load_plan_input(source, plan) || plan.tasks.size() < 2) {
        return;
    }
    const std::uint32_t doomed = plan.tasks.back().uid;
    const std::uint32_t kept = plan.tasks.front().uid;

    app::StructuralEdits edits;
    check(edits.push(delete_edit(doomed)), "the delete + formula delete is queued");

    app::CustomFormulaEdit formula;
    formula.operation = app::CustomFormulaEdit::Operation::set;
    formula.field_id = 0x0b400036u;  // Text2
    formula.formula = "[Name]";
    const app::StructuralSaveResult outcome = app::save_structural_edits(
        edits, {}, source.string(), target.string(), false,
        document_edit_request(app::ViewStateEdits{}, formula));
    print_cascade("delete-formula", outcome);
    check(outcome.ok, "a delete + formula change saves in one write");
    if (!outcome.ok) {
        return;
    }
    check_size(outcome.deleted, 1, "the delete landed");
    check(outcome.document_edits_written, "the formula rode the same save");
    check(outcome.document_edits.formula_written, "the formula was written");
    check(!outcome.document_edits.view_state_written, "no view-state edit was requested");
    check(outcome.api.find("CfbDocument::save") != std::string::npos,
          "the delete + formula save ended with the explicit container save");
    check(!std::filesystem::exists(target.string() + ".w51-structural-tmp"),
          "the delete + formula save left no intermediate file");

    try {
        const mpp::Document reread = mpp::load_file(target.string());
        check_size(reread.tasks.size(), plan.tasks.size() - 1,
                   "the reopened file has one task fewer");
        check(task_with_uid(reread, doomed) == nullptr, "the deleted task is gone");
        check(task_with_uid(reread, kept) != nullptr, "the surviving task is still there");
    } catch (const std::exception& error) {
        check(false, ("the delete + formula file does not load: " + std::string(error.what()))
                         .c_str());
    }

    const std::vector<std::uint8_t> props = stream_bytes(target, "114/Props");
    msproject::props::PropsDocument parsed;
    msproject::props::ParseError parse_error;
    check(msproject::props::parse_props(props, parsed, &parse_error),
          "the delete + formula file's 114/Props parses");
    const std::vector<msproject::props::FormulaBlock> formulas =
        msproject::props::extract_formulas(parsed);
    check_size(formulas.size(), 1, "one formula reads back");
    if (!formulas.empty()) {
        check_size(formulas[0].field_id, 0x0b400036u, "the formula is on Text2");
        check_text(formulas[0].formula, "[MSPJ188743694]",
                   "the formula stores [Name] as its measured MSPJ token");
    }
    std::uint16_t flag = 0;
    check(props_flag(props, flag), "the delete + formula file has the flag record");
    check_size(flag, 1, "setting a formula leaves the flag at 1");
    check(stream_bytes(source, "214/CTable/Var2Data") ==
              stream_bytes(target, "214/CTable/Var2Data"),
          "the delete + formula save leaves the table state alone");
    check_size(bytes_on_disk(source), source_bytes, "the source fixture is untouched");
}

#else
void test_save_round_trip() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
}

void test_save_view_state_date_format() {}
void test_save_view_state_column() {}
void test_save_without_edits() {}
void test_save_formula_set() {}
void test_save_formula_clear_with_values() {}
void test_save_formula_clear_value_free() {}
void test_save_refused_view_state() {}
void test_document_request_validation() {}
void test_save_formula_and_view_state() {}
void test_save_start_date_with_view_and_formula() {}
void test_cascade_without_hook_is_unchanged() {}
void test_cascade_with_document_edits_in_one_write() {}
void test_cascade_hook_refusal() {}
void test_cascade_delete_with_formula() {}
#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_default_save_name();
    test_collect_start_edits();
    test_save_round_trip();
    test_save_view_state_date_format();
    test_save_view_state_column();
    test_save_without_edits();
    test_save_formula_set();
    test_save_formula_clear_with_values();
    test_save_formula_clear_value_free();
    test_save_refused_view_state();
    test_document_request_validation();
    test_save_start_date_with_view_and_formula();
    test_save_formula_and_view_state();
    test_cascade_without_hook_is_unchanged();
    test_cascade_with_document_edits_in_one_write();
    test_cascade_hook_refusal();
    test_cascade_delete_with_formula();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("document_tests: %d checks passed\n", g_checks);
    return 0;
}
