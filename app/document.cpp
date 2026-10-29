// app/document.cpp -- see document.h for the contract, the save semantics and
// which parts of an edit persist.
//
// No AppKit and no reader types: this file talks to core/view (the RecordSet it
// diffs), core/mppwrite (the byte writer), core/viewstate (the `214/CTable`
// writer), core/props (the `114/Props` formula writer) and the filesystem.  That
// is what lets app/document_tests.cpp run the whole save path on Windows, where
// AppKit does not exist.

#include "document.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "field_ref.h"
#include "field_value.h"
#include "props.h"
#include "row_writer.h"
#include "table.h"
#include "viewstate.h"

#if APP_MPPWRITE_SAVE_HEADER
#include "save.h"
#endif

namespace app {

// W33-1.3 (A-5, High): only Text1..30 and Number1..20 formula SETs were
// measured against Project.  Kept above the anonymous namespace so the save
// hook's defence-in-depth check below can use the same decision as the queue.
bool custom_formula_field_is_settable(std::uint32_t field_id) noexcept {
    const mpp::FieldKind kind = mpp::custom_field_kind(field_id);
    return kind == mpp::FieldKind::text || kind == mpp::FieldKind::number;
}

std::string custom_formula_set_refusal(std::uint32_t field_id) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%08x", static_cast<unsigned>(field_id));
    const std::string name =
        core::view::field_title(field_id, core::model::RecordType::Task);
    std::string field = name;
    if (name != hex) {
        field += " (" + std::string(hex) + ")";
    }
    return "a formula on " + field +
           " is not implemented in this port: only Text1..30 and Number1..20 formula "
           "storage was measured against Project";
}

std::string view_state_date_format_refusal(std::uint32_t table_index) {
    return "a date format for table " + std::to_string(table_index) +
           " is not implemented in this port: only the first table's date-format "
           "write is measured against Project";
}

// W33b-1.1 (R33-2, Medium): the decision-9 reason a formula CLEAR is refused.
// Shared by the queue (`Document::clear_custom_formula`) and the save hook, so a
// hand-built request reports the same words the shells show.
std::string custom_formula_clear_refusal() {
    return "clearing formulas is not implemented in this port: the 0x02401392 "
           "custom-field-data flag's meaning is unmeasured, so the empty formula "
           "table is not written";
}

namespace {

using core::view::Value;
using core::view::ValueKind;

constexpr std::string_view kTaskTable = "114/TBkndTask";
constexpr std::string_view kAssignmentTable = "114/TBkndAssn";
constexpr std::string_view kPropsStream = "114/Props";

// The four independent top-level streams of one `214/<Class>` collection, in the
// order ViewState / EncodedParts use.  Only CTable is written by this stream.
constexpr std::array<std::string_view, 4> kCTableParts = {
    "214/CTable/FixedData",
    "214/CTable/FixedMeta",
    "214/CTable/VarMeta",
    "214/CTable/Var2Data",
};

// The measured Task-table geometry, repeated here only to skip the three
// non-task FixedMeta entries when looking for a Flag1 value.
constexpr std::size_t kTaskFixedMetaHeader = 16;
constexpr std::size_t kTaskFixedMetaFirstRowEntry = 3;
constexpr std::uint8_t kFlag1Bit = 0x02;
constexpr std::size_t kFlag1ByteInEntry = 24;

// The measured start of the ASSIGNMENT custom-field key space (W60):
// Assignment.Number1 = 0x0f40403c, Assignment.Text1 = 0x0f404064.  Keys below
// this are the assignment's ordinary properties, not custom values.
constexpr std::uint32_t kAssignmentCustomKeyBase = 0x0F40403Cu;

std::filesystem::path normalised(const std::string& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return std::filesystem::path(path).lexically_normal();
    }
    return absolute.lexically_normal();
}

std::string uid_text(std::uint32_t uid) {
    return "task uid " + std::to_string(uid);
}

std::uint32_t get_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

// True when the Task table (or the assignment table) still stores a custom-field
// VALUE.  This is the app-side half of W59's measured rule: the writer sets the
// shared "custom-field data in use" flag (0x02401392) to 0 on a real clear, but
// the flag must stay 1 while any stored value remains.
//
// What counts, from the document's own task table:
//   * a `114/TBkndTask` VarMeta entry whose key is one of the measured Task
//     custom-field ids (Text/Number/Duration/Date/Cost/Flag families);
//   * a set Flag1 bit in a task row's FixedMeta entry (a flag has no VarMeta
//     block; the bit is the value);
//   * a `114/TBkndAssn` VarMeta entry in the measured assignment custom-key
//     range.
// A table that cannot be read at all answers "yes": keeping the flag at 1 is the
// safe direction (the alternative tells Project there is no custom-field data
// while some may exist).  A cleared field keeps a stale VarMeta entry (W52), so
// this predicate can stay true after a value clear; that is conservative by
// design and the flag is only about "custom-field data in use", not about a
// specific value.
bool custom_field_value_remains(const mppwrite::CfbDocument& document) {
    try {
        if (document.has_stream(std::string(kTaskTable) + "/VarMeta")) {
            const std::vector<std::uint8_t> var_meta =
                document.read_stream(std::string(kTaskTable) + "/VarMeta");
            for (const mppwrite::VarEntry& entry : mppwrite::read_var_entries(var_meta)) {
                if (mpp::custom_field_kind(entry.field_id) != mpp::FieldKind::unknown) {
                    return true;
                }
            }
        }

        if (document.has_stream(std::string(kTaskTable) + "/FixedMeta")) {
            const std::vector<std::uint8_t> meta =
                document.read_stream(std::string(kTaskTable) + "/FixedMeta");
            if (meta.size() >= kTaskFixedMetaHeader) {
                const std::uint32_t declared = get_u32(meta, 8);
                const std::size_t body = meta.size() - kTaskFixedMetaHeader;
                if (declared > 0 && body % declared == 0) {
                    const std::size_t entry_size = body / declared;
                    if (entry_size >= kFlag1ByteInEntry + 1) {
                        for (std::uint32_t i = static_cast<std::uint32_t>(
                                 kTaskFixedMetaFirstRowEntry);
                             i < declared; ++i) {
                            const std::size_t at = kTaskFixedMetaHeader +
                                                   static_cast<std::size_t>(i) * entry_size +
                                                   kFlag1ByteInEntry;
                            if ((meta[at] & kFlag1Bit) != 0) {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        if (document.has_stream(std::string(kAssignmentTable) + "/VarMeta")) {
            const std::vector<std::uint8_t> var_meta =
                document.read_stream(std::string(kAssignmentTable) + "/VarMeta");
            for (const mppwrite::VarEntry& entry : mppwrite::read_var_entries(var_meta)) {
                if (entry.field_id >= kAssignmentCustomKeyBase) {
                    return true;
                }
            }
        }
    } catch (const std::exception&) {
        return true;  // unreadable table: keep the flag, never guess it down
    }
    return false;
}

// Sets the payload of the measured 0x02401392 flag record (kind 2, u16).  There
// is no core/props API for this: W59's writers own the flag only as a side effect
// (set -> 1, real clear -> 0) and explicitly leave the value-only case to the
// app.  Parsing locates the record; the patch is two bytes inside its payload, so
// the stream's size words and every other record are untouched.
bool set_custom_field_flag(std::vector<std::uint8_t>& props, std::uint16_t value,
                           std::string& error) {
    msproject::props::PropsDocument parsed;
    msproject::props::ParseError parse_error;
    if (!msproject::props::parse_props(props, parsed, &parse_error)) {
        error = "114/Props does not parse: " + parse_error.message;
        return false;
    }
    const msproject::props::PropsRecord* record =
        parsed.find(msproject::props::kCustomFieldFlagRecordId);
    if (record == nullptr) {
        error = "114/Props has no custom-field flag record (0x02401392)";
        return false;
    }
    if (record->kind != 2u || record->payload_length != 2u) {
        error = "114/Props flag record 0x02401392 is not a 2-byte integer";
        return false;
    }
    const std::size_t at = static_cast<std::size_t>(record->offset) + 12u;
    if (at + 2u > props.size()) {
        error = "114/Props flag record 0x02401392 runs past the stream";
        return false;
    }
    props[at] = static_cast<std::uint8_t>(value & 0xFFu);
    props[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    return true;
}

// The start edits that can actually be written, with the same warnings the
// existing save path always emitted (a cleared cell has no date; uid 0 is the
// project row).
std::vector<StartEdit> writable_start_edits(const std::vector<StartEdit>& edits,
                                            SaveOutcome& outcome) {
    std::vector<StartEdit> writable;
    writable.reserve(edits.size());
    for (const StartEdit& edit : edits) {
        if (edit.start == kDateNa) {
            outcome.warnings.push_back(uid_text(edit.uid) +
                                       ": start cleared in the sheet -- no date to write");
        } else if (edit.uid == 0) {
            outcome.warnings.push_back("task uid 0 is the project row and is not written");
        } else {
            writable.push_back(edit);
        }
    }
    return writable;
}

// Applies the formula request.  A refusal here fails the whole save: a formula
// the user asked for must never be dropped silently.
void apply_formula_edit(mppwrite::CfbDocument& document, const CustomFormulaEdit& edit,
                        DocumentEditOutcome& outcome) {
    if (!edit.any()) {
        return;
    }
    // W33b-1.1 (R33-2, Medium): W33-1.4 disabled the clear on Document and in
    // both shells, but a hand-built request still reached the unmeasured writer
    // below (`clear_custom_field_formula` writes the 0x02401392 flag to 0, and
    // what that means is unmeasured).  Refuse it here exactly as W33-1.3's
    // backstop refuses an unmeasured SET, before the stream is read: the
    // container is untouched and the caller must not save.  The measured-clear
    // implementation is kept beneath this refusal so a future Project pair can
    // lift it without reconstructing the flag decision.
    if (edit.operation == CustomFormulaEdit::Operation::clear) {
        outcome.ok = false;
        outcome.error = "formulas not cleared: " + custom_formula_clear_refusal();
        return;
    }
    if (!document.has_stream(kPropsStream)) {
        outcome.ok = false;
        outcome.error = "114/Props is missing; a formula cannot be written into this file";
        return;
    }

    const std::vector<std::uint8_t> props = document.read_stream(kPropsStream);
    std::vector<std::uint8_t> updated;
    std::string writer_error;

    if (edit.operation == CustomFormulaEdit::Operation::set) {
        // W33-1.3 (A-5, High): a request that skipped Document's queue (a
        // hand-built CustomFormulaEdit) is refused by the same rule, so no save
        // can write a formula store Project has not been measured to accept.
        if (!custom_formula_field_is_settable(edit.field_id)) {
            outcome.ok = false;
            outcome.error = "formula not written: " + custom_formula_set_refusal(edit.field_id);
            return;
        }
        if (!msproject::props::set_custom_field_formula(props, edit.field_id, edit.formula,
                                                        updated, &writer_error)) {
            outcome.ok = false;
            outcome.error = "formula not written: " + writer_error;
            return;
        }
    } else {
        msproject::props::PropsDocument parsed;
        msproject::props::ParseError parse_error;
        if (!msproject::props::parse_props(props, parsed, &parse_error)) {
            outcome.ok = false;
            outcome.error = "114/Props does not parse: " + parse_error.message;
            return;
        }
        if (msproject::props::extract_formulas(parsed).empty()) {
            // Nothing to remove.  Do not rewrite the stream: a no-op clear must
            // leave the flag and every byte alone (W59's measured value-only
            // case).
            updated = props;
        } else {
            if (!msproject::props::clear_custom_field_formula(props, updated, &writer_error)) {
                outcome.ok = false;
                outcome.error = "formulas not cleared: " + writer_error;
                return;
            }
            if (custom_field_value_remains(document)) {
                std::string flag_error;
                if (!set_custom_field_flag(updated, 1, flag_error)) {
                    outcome.ok = false;
                    outcome.error = "formulas cleared, but the custom-field flag could not be "
                                    "kept at 1: " + flag_error;
                    return;
                }
                outcome.custom_field_flag_restored = true;
                outcome.warnings.push_back(
                    "custom-field data flag (0x02401392) kept at 1: the project still stores "
                    "custom-field values");
            }
        }
    }

    if (updated != props) {
        document.replace_stream(kPropsStream, std::move(updated));
        outcome.formula_written = true;
    }
}

// Applies the `214/CTable` request.  A refusal is a warning only: the request is a
// presentation preference, the file remains valid, and `214/` is left untouched.
void apply_view_state_edit(mppwrite::CfbDocument& document, const ViewStateEdits& edits,
                           DocumentEditOutcome& outcome) {
    if (!edits.any()) {
        return;
    }
    // W33b-0.1 (R33-1, High): defence in depth for a hand-built request that
    // went around Document's queue.  Only table 0's date-format byte has a
    // Project save pair; every other index is refused, and the 214/ tree is
    // left byte-identical (the view-state contract: a refusal is a warning).
    if (edits.set_date_format && edits.table_index != 0) {
        outcome.warnings.push_back("view state not written: " +
                                   view_state_date_format_refusal(edits.table_index));
        return;
    }
    try {
        std::array<std::vector<std::uint8_t>, 4> raw;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (!document.has_stream(kCTableParts[i])) {
                outcome.warnings.push_back("view state not written: " +
                                           std::string(kCTableParts[i]) + " is missing");
                return;
            }
            raw[i] = document.read_stream(kCTableParts[i]);
        }

        msproject::viewstate::PartSet parts;
        parts.fixed_data = raw[0];
        parts.fixed_meta = raw[1];
        parts.var_meta = raw[2];
        parts.var2_data = raw[3];

        msproject::viewstate::ViewState state;
        msproject::viewstate::ParseError parse_error;
        if (!msproject::viewstate::parse_view_state("CTable", parts, state, &parse_error)) {
            outcome.warnings.push_back("view state not written: 214/CTable does not parse (" +
                                       parse_error.message + ")");
            return;
        }

        // Both edits are applied to a COPY, so one refusal (e.g. a bad column
        // index) leaves neither edit in the output.
        msproject::viewstate::ViewState working = state;
        msproject::viewstate::WriteError write_error;

        if (edits.set_date_format) {
            if (!msproject::viewstate::set_table_date_format(
                    working, edits.table_index, edits.date_format, &write_error)) {
                outcome.warnings.push_back("view state not written: " + write_error.message);
                return;
            }
        }
        if (edits.replace_column) {
            if (!msproject::viewstate::set_table_column(working, edits.table_index,
                                                        edits.column_index, edits.field_id,
                                                        edits.title, &write_error)) {
                outcome.warnings.push_back("view state not written: " + write_error.message);
                return;
            }
        }

        msproject::viewstate::EncodedParts encoded;
        if (!msproject::viewstate::encode_view_state(working, encoded, &write_error)) {
            outcome.warnings.push_back("view state not written: " + write_error.message);
            return;
        }

        std::array<std::vector<std::uint8_t>*, 4> encoded_parts = {
            &encoded.fixed_data, &encoded.fixed_meta, &encoded.var_meta, &encoded.var2_data};
        bool changed = false;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (*encoded_parts[i] != raw[i]) {
                document.replace_stream(kCTableParts[i], std::move(*encoded_parts[i]));
                changed = true;
            }
        }
        outcome.view_state_written = changed;
    } catch (const std::exception& error) {
        outcome.warnings.push_back(std::string("view state not written: ") + error.what());
    }
}

}  // namespace

std::vector<StartEdit> collect_start_edits(const core::view::RecordSet& records,
                                           const PlanInput& loaded) {
    std::vector<StartEdit> edits;
    const core::view::FieldId start_field = task_field_id("Start");
    if (start_field == 0) {
        return edits;  // the descriptor table lost Start: nothing can be diffed
    }
    const std::size_t rows = records.row_count();
    edits.reserve(rows);
    for (std::size_t row = 0; row < rows; ++row) {
        const Value* value = records.get(row, start_field);
        if (value == nullptr) {
            continue;
        }
        const std::uint32_t uid = records.uid(row);
        const TaskRow* loaded_row = nullptr;
        for (const TaskRow& candidate : loaded.tasks) {
            if (candidate.uid == uid) {
                loaded_row = &candidate;
                break;
            }
        }
        if (loaded_row == nullptr) {
            continue;  // a pending insert: not a file row, nothing to write here
        }
        const date32 current =
            value->kind == ValueKind::Date ? date32_from_date_value(value->date) : kDateNa;
        if (current == loaded_row->start) {
            continue;
        }
        StartEdit edit;
        edit.uid = uid;
        edit.start = current;
        edits.push_back(edit);
    }
    return edits;
}

std::string default_save_name(std::string_view source_path) {
    std::string stem = std::filesystem::path(std::string(source_path)).stem().string();
    if (stem.empty()) {
        stem = "project";
    }
    return stem + "-edited.mpp";
}

SaveOutcome save_start_edits(const std::vector<StartEdit>& edits,
                             std::string_view source_path, std::string_view target_path,
                             const SaveOptions& options) {
    SaveOutcome outcome;
    outcome.path = std::string(target_path);
    outcome.requested = edits.size();

    if (target_path.empty()) {
        outcome.error = "no target path";
        return outcome;
    }
    if (!options.allow_source_overwrite &&
        normalised(std::string(source_path)) == normalised(outcome.path)) {
        outcome.error =
            "refusing to overwrite the opened file; save to a new name (the opened .mpp is "
            "never modified)";
        return outcome;
    }

    // Only a real date can be written.  A cleared Start is reported rather than
    // silently dropped, and rows with no date are not counted as written.
    const std::vector<StartEdit> writable = writable_start_edits(edits, outcome);
    if (writable.empty()) {
        outcome.ok = true;  // nothing to write is a success, not a failure
        return outcome;
    }

    try {
        mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(std::string(source_path));
#if APP_MPPWRITE_SAVE_HEADER
        std::vector<mppwrite::DateEdit> date_edits;
        date_edits.reserve(writable.size());
        for (const StartEdit& edit : writable) {
            mppwrite::DateEdit date_edit;
            date_edit.uid = edit.uid;
            date_edit.start = packed_from_date32(edit.start);
            date_edits.push_back(date_edit);
        }
        const mppwrite::SaveResult result =
            mppwrite::save_date_edits(document, date_edits, outcome.path);
        outcome.rows_written = result.rows_written;
        // The writer's messages are receipts (a post-save re-read per row) plus
        // its own manual-row warning; they are logged, not treated as failures.
        for (const std::string& note : result.warnings) {
            outcome.notes.push_back(note);
        }
        outcome.api = "mppwrite::save_date_edits";
#else
        const mppwrite::TaskTable table = mppwrite::TaskTable::from_document(document);
        for (const StartEdit& edit : writable) {
            const std::size_t row = table.row_for_uid(edit.uid);  // throws when absent
            mppwrite::pin_task_start(document, row, packed_from_date32(edit.start));
            ++outcome.rows_written;
        }
        document.save(outcome.path);  // one write, so a refusal leaves no file
        outcome.api = "mppwrite::pin_task_start + CfbDocument::save";
#endif
        // The writer's own consistency report.  A file that already carries
        // damage (the corpus PROJECT_A file does) is still saveable, so this is a
        // warning, not a refusal.
        const std::string report = mppwrite::check_table(document);
        if (!report.empty()) {
            outcome.warnings.push_back("table check: " + report);
        }
        outcome.ok = true;
    } catch (const std::exception& error) {
        outcome.ok = false;
        outcome.rows_written = 0;
        outcome.error = error.what();
    }
    return outcome;
}

void accept_start_edits(PlanInput& loaded, const std::vector<StartEdit>& edits) {
    for (const StartEdit& edit : edits) {
        if (edit.start == kDateNa) {
            continue;
        }
        for (TaskRow& row : loaded.tasks) {
            if (row.uid == edit.uid) {
                row.start = edit.start;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Wave 15: view-state and formula operations
// ---------------------------------------------------------------------------

DocumentEditOutcome apply_document_edits(mppwrite::CfbDocument& document,
                                         const ViewStateEdits& view,
                                         const CustomFormulaEdit& formula) {
    DocumentEditOutcome outcome;
    try {
        apply_formula_edit(document, formula, outcome);
        if (!outcome.ok) {
            return outcome;
        }
        apply_view_state_edit(document, view, outcome);
    } catch (const std::exception& error) {
        outcome.ok = false;
        outcome.error = error.what();
    }
    return outcome;
}

Document::Document(std::string source_path) : source_path_(std::move(source_path)) {}

bool Document::request_view_state_edits(const ViewStateEdits& edits, std::string* error) {
    // W33b-0.1 (R33-1, High): W33-1.6 carried the shown table index into the
    // save, but only table 0's date-format byte has a Project save pair (the
    // col -> date blobs; w64.md lists every other index as unmeasured).  Refuse
    // before anything is queued, and leave a request queued before it alone.
    if (edits.set_date_format && edits.table_index != 0) {
        if (error != nullptr) {
            *error = view_state_date_format_refusal(edits.table_index);
        }
        return false;
    }
    view_state_edits_ = edits;
    return true;
}

void Document::discard_view_state_edits() {
    view_state_edits_ = ViewStateEdits{};
}

bool Document::apply_custom_formula(std::uint32_t field_id, std::string_view formula,
                                    std::string* error) {
    if (field_id == 0) {
        if (error != nullptr) {
            *error = "field id 0 is not a task custom field";
        }
        return false;
    }
    if (!custom_formula_field_is_settable(field_id)) {
        if (error != nullptr) {
            *error = custom_formula_set_refusal(field_id);
        }
        return false;
    }
    if (formula.empty()) {
        if (error != nullptr) {
            *error = "the formula is empty";
        }
        return false;
    }
    // W33-1.8 (A-12, Low): one pending formula operation at a time.  A second
    // valid request used to replace the first silently; it is refused with the
    // reason instead, and the first stays queued until Save writes it or the
    // shell discards it.  The field-kind refusal above keeps its own reason, so
    // a request this port cannot store is still reported as unimplemented.
    if (custom_formula_.any()) {
        if (error != nullptr) {
            *error = "a custom-field formula is already pending; save it before setting "
                     "another";
        }
        return false;
    }
    custom_formula_.operation = CustomFormulaEdit::Operation::set;
    custom_formula_.field_id = field_id;
    custom_formula_.formula = std::string(formula);
    return true;
}

bool Document::clear_custom_formula(std::string* error) {
    // W33-1.4 (A-4, High): core/props' clear writes the 0x02401392
    // "custom-field data in use" flag to 0, and what Project means by 0 is
    // unmeasured.  The measured write is SET's unconditional 1; until a Project
    // pair measures a clear, the operation is disabled with decision 9's text.
    // W33b-1.1 (R33-2, Medium): the save hook refuses the same operation, so a
    // hand-built CustomFormulaEdit cannot reach the clear writer either.
    if (error != nullptr) {
        *error = custom_formula_clear_refusal();
    }
    return false;
}

void Document::discard_custom_formula() {
    custom_formula_ = CustomFormulaEdit{};
}

SaveOutcome Document::save(std::string_view target_path, const SaveOptions& options) const {
    return save(std::vector<StartEdit>{}, target_path, options);
}

SaveOutcome Document::save(const std::vector<StartEdit>& start_edits,
                           std::string_view target_path, const SaveOptions& options) const {
    SaveOutcome outcome;
    outcome.path = std::string(target_path);
    outcome.requested = start_edits.size() + (view_state_edits_.any() ? 1u : 0u) +
                        (custom_formula_.any() ? 1u : 0u);

    if (target_path.empty()) {
        outcome.error = "no target path";
        return outcome;
    }
    if (!options.allow_source_overwrite &&
        normalised(source_path_) == normalised(outcome.path)) {
        outcome.error =
            "refusing to overwrite the opened file; save to a new name (the opened .mpp is "
            "never modified)";
        return outcome;
    }

    const std::vector<StartEdit> writable = writable_start_edits(start_edits, outcome);

    try {
        mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(source_path_);
        const DocumentEditOutcome edits =
            apply_document_edits(document, view_state_edits_, custom_formula_);
        outcome.view_state_written = edits.view_state_written;
        outcome.formula_written = edits.formula_written;
        outcome.custom_field_flag_restored = edits.custom_field_flag_restored;
        for (const std::string& warning : edits.warnings) {
            outcome.warnings.push_back(warning);
        }
        if (!edits.ok) {
            outcome.ok = false;
            outcome.error = edits.error;
            return outcome;
        }

        if (writable.empty()) {
            const std::string report = mppwrite::check_table(document);
            if (!report.empty()) {
                outcome.warnings.push_back("table check: " + report);
            }
            const std::vector<std::uint8_t> written = document.to_bytes();
            document.save(outcome.path);
            // Prove the container from disk: the file must re-emit byte-identically.
            const mppwrite::CfbDocument reread =
                mppwrite::CfbDocument::from_file(outcome.path);
            if (reread.to_bytes() != written) {
                outcome.ok = false;
                outcome.error = "the written file does not re-emit byte-identically";
                return outcome;
            }
            outcome.api = "mppwrite::CfbDocument + core/viewstate + core/props";
        } else {
            // The date writer applies its pins to a copy of THIS document, so the
            // formula/view-state streams it verifies as "unchanged" are ours.
#if APP_MPPWRITE_SAVE_HEADER
            std::vector<mppwrite::DateEdit> date_edits;
            date_edits.reserve(writable.size());
            for (const StartEdit& edit : writable) {
                mppwrite::DateEdit date_edit;
                date_edit.uid = edit.uid;
                date_edit.start = packed_from_date32(edit.start);
                date_edits.push_back(date_edit);
            }
            const mppwrite::SaveResult result =
                mppwrite::save_date_edits(document, date_edits, outcome.path);
            outcome.rows_written = result.rows_written;
            for (const std::string& note : result.warnings) {
                outcome.notes.push_back(note);
            }
            outcome.api = "mppwrite::save_date_edits + core/viewstate + core/props";
#else
            const mppwrite::TaskTable table = mppwrite::TaskTable::from_document(document);
            for (const StartEdit& edit : writable) {
                const std::size_t row = table.row_for_uid(edit.uid);  // throws when absent
                mppwrite::pin_task_start(document, row, packed_from_date32(edit.start));
                ++outcome.rows_written;
            }
            document.save(outcome.path);
            outcome.api = "mppwrite::pin_task_start + core/viewstate + core/props";
#endif
        }
        outcome.ok = true;
    } catch (const std::exception& error) {
        outcome.ok = false;
        outcome.rows_written = 0;
        outcome.error = error.what();
    }
    return outcome;
}

StructuralSaveResult Document::save_structural(const StructuralEdits& edits,
                                               const std::vector<StartEdit>& start_edits,
                                               std::string_view target_path,
                                               bool allow_source_overwrite) const {
    StructuralDocumentEdits request;
    if (has_pending_edits()) {
        request.view = view_state_edits_;
        request.formula = custom_formula_;
        // `apply_document_edits` has exactly the hook's signature: it mutates the
        // cascade's CfbDocument and does no file I/O.  The cascade writes once,
        // after it.  Nothing pending leaves the hook null, so the cascade takes
        // its pre-wave-17 path.
        request.hook = apply_document_edits;
    }
    return save_structural_edits(edits, start_edits, source_path_, target_path,
                                 allow_source_overwrite, request);
}

}  // namespace app
