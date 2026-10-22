// app/document.cpp -- see document.h for the contract, the save semantics and
// which parts of an edit persist.
//
// No AppKit and no reader types: this file talks to core/view (the RecordSet it
// diffs), core/mppwrite (the byte writer) and the filesystem.  That is what lets
// app/document_tests.cpp run the whole save path on Windows, where AppKit does
// not exist.

#include "document.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "row_writer.h"
#include "table.h"

#if APP_MPPWRITE_SAVE_HEADER
#include "save.h"
#endif

namespace app {
namespace {

using core::view::Value;
using core::view::ValueKind;

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

}  // namespace app
