/*
 * save.cpp -- the save flow.  See save.h for the contract and the measured
 * facts it rests on.
 *
 * The sequence, once per entry point:
 *
 *   1. validate every edit against the CURRENT document (uid exists, not the
 *      project row, unique, valid packed date) and the output path;
 *   2. apply the edits to a copy of the document, so a refusal cannot leave the
 *      caller's document half-written;
 *   3. prove from the two documents that only the intended bytes moved;
 *   4. save;
 *   5. re-open the file from disk and prove, through the public readers, that
 *      the bytes on disk carry the change -- the re-read receipt goes into
 *      SaveResult::warnings, a mismatch is an exception.
 *
 * Step 5 is the point of this file.  "I called save" is not evidence; "I read
 * the uid back out of the file on disk and the constraint and every row word
 * match" is.  Nothing here is specific to a particular .mpp: the table path is
 * an argument, row 0 is the project row, and the only column this flow writes
 * is the constraint pair at FixedData +64/+66 (or, for save_plan, whatever
 * recalculate_write_path writes).
 */

#include "save.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mppwrite {
namespace {

std::string table_stream(std::string_view table, std::string_view name) {
    return std::string(table) + "/" + std::string(name);
}

bool contains(std::vector<std::uint32_t> const& values, std::uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string uid_label(std::uint32_t uid) {
    return "uid " + std::to_string(uid);
}

std::string join_offsets(std::vector<std::size_t> const& offsets) {
    std::string out;
    for (std::size_t const offset : offsets) {
        if (!out.empty()) {
            out += ", ";
        }
        out += std::to_string(offset);
    }
    return out;
}

std::uint32_t get_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

/*
 * The FixedData offsets named by the table's FixedMeta entries.  FixedMeta is
 * per-row bookkeeping, not a field map: 16-byte header, then 47-byte entries
 * whose +4 word is the FixedData row offset (entry i >= 3 names row i - 3;
 * table.h's consistency rule).  The UniqueID lives in FixedData +4, so
 * "uid -> row" is resolved by finding the row whose UniqueID matches and then
 * checking that a FixedMeta entry covers that row's offset -- the reader's
 * liveness rule (core/mpp/reader.cpp): a stored row that no entry names is
 * treated as deleted by Project and would never be scheduled, so a constraint
 * written on it would be dead.
 */
std::vector<std::uint32_t> fixed_meta_offsets(CfbDocument const& doc,
                                              std::string_view table,
                                              std::string const& function) {
    std::string const path = table_stream(table, "FixedMeta");
    if (!doc.has_stream(path)) {
        throw Error(function + ": the table has no " + path + " stream");
    }
    std::vector<std::uint8_t> const meta = doc.read_stream(path);
    if (meta.size() < 16) {
        throw Error(function + ": " + path + " is " +
                    std::to_string(meta.size()) +
                    " bytes, shorter than its 16-byte header");
    }
    std::vector<std::uint32_t> offsets;
    for (std::size_t base = 16; base + 47 <= meta.size(); base += 47) {
        offsets.push_back(get_u32(meta, base + 4));
    }
    return offsets;
}

/* Fixed2Meta: 16-byte header, then 96-byte entries paired with FixedMeta by
 * ENTRY INDEX (core/mpp/reader.cpp) -- not by array position.  Entry byte +8 is
 * the per-row flags byte. */
constexpr std::size_t kFixed2MetaHeaderSize = 16;
constexpr std::size_t kFixed2MetaEntrySize = 96;
constexpr std::size_t kFixed2MetaFlags = 8;
constexpr std::uint8_t kManualFlag = 0x80;

/*
 * A row's stored scheduling mode.  The writer's handle is bit 0x80 of the
 * Fixed2Meta per-row flags byte -- MEASURED 2026-10-22, not an inference any
 * more: clearing it on a Manual=True row and writing SNET made Project report
 * Manual=False for that row and schedule it on the constraint
 * (corpus/probe/w37-snet-auto.mpp; the coordinator's read-only open).  The
 * reader reached the same bit independently (core/mpp/reader.h, now upgraded
 * from inference to measured).  `known` is false when the Fixed2Meta stream or
 * the row's entry is unreadable; the caller then warns instead of converting.
 */
struct ManualState {
    bool known = false;     /* the flag byte could be read */
    bool manual = false;    /* bit 0x80 set: stored as manually scheduled */
    std::size_t offset = 0; /* Fixed2Meta stream offset of the flags byte */
};

ManualState read_manual_state(CfbDocument const& doc, std::size_t row,
                              std::string_view table,
                              std::string const& function) {
    ManualState state;
    std::string const f2_meta_path = table_stream(table, "Fixed2Meta");
    if (!doc.has_stream(f2_meta_path)) {
        return state;
    }
    std::vector<std::uint32_t> const offsets =
        fixed_meta_offsets(doc, table, function);
    std::uint32_t const wanted = static_cast<std::uint32_t>(task_row_offset(row));
    auto const found = std::find(offsets.begin(), offsets.end(), wanted);
    if (found == offsets.end()) {
        return state;
    }
    std::size_t const index =
        static_cast<std::size_t>(found - offsets.begin());
    std::vector<std::uint8_t> const f2_meta = doc.read_stream(f2_meta_path);
    std::size_t const entry = kFixed2MetaHeaderSize + index * kFixed2MetaEntrySize;
    if (entry + kFixed2MetaFlags + 1 > f2_meta.size()) {
        return state;
    }
    state.known = true;
    state.offset = entry + kFixed2MetaFlags;
    state.manual = (f2_meta[state.offset] & kManualFlag) != 0;
    return state;
}


/*
 * Refuse an output path that cannot be written, before anything is saved.  The
 * directory is required to exist: creating parent directories is the caller's
 * decision, not a side effect of saving, and a missing directory is a much more
 * common mistake than a deliberate tree.
 */
void check_output_path(std::string const& path, std::string const& function) {
    if (path.empty()) {
        throw Error(function + ": the output path is empty");
    }
    std::filesystem::path const out(path);
    if (out.filename().empty()) {
        throw Error(function + ": " + path +
                    " names a directory, not a file to save to");
    }
    std::error_code error;
    if (std::filesystem::is_directory(out, error)) {
        throw Error(function + ": " + path + " is a directory");
    }
    std::filesystem::path const parent = out.parent_path();
    if (parent.empty()) {
        return; /* the current directory; save() will report any failure */
    }
    error.clear();
    bool const parent_exists = std::filesystem::exists(parent, error);
    if (error || !parent_exists) {
        throw Error(function + ": cannot save to " + path + ": the directory " +
                    parent.string() + " does not exist");
    }
    error.clear();
    if (!std::filesystem::is_directory(parent, error)) {
        throw Error(function + ": cannot save to " + path + ": " +
                    parent.string() + " is not a directory");
    }
}

/*
 * Every stream of `before` must still exist in `after` and be byte-identical,
 * except the paths in `allowed`.
 */
void require_streams_unchanged(CfbDocument const& before, CfbDocument const& after,
                               std::vector<std::string> const& allowed,
                               std::string const& function) {
    for (std::string const& stream : before.stream_paths()) {
        if (std::find(allowed.begin(), allowed.end(), stream) != allowed.end()) {
            continue;
        }
        if (!after.has_stream(stream)) {
            throw Error(function + ": stream " + stream + " vanished");
        }
        if (before.read_stream(stream) != after.read_stream(stream)) {
            throw Error(function + ": stream " + stream +
                        " changed; the edit must not touch it");
        }
    }
}

/*
 * save_date_edits' byte contract: FixedData may change only in the 6-byte
 * constraint window (ConstraintType u16 at +64, ConstraintDate u32 at +66) of
 * the edited rows; Fixed2Meta may change only at the listed manual-mode flag
 * bytes; no other stream may differ.
 */
void require_only_edit_bytes(CfbDocument const& before,
                             CfbDocument const& after,
                             std::vector<std::size_t> const& constraint_rows,
                             std::vector<std::size_t> const& fixed2meta_offsets,
                             std::string_view table,
                             std::string const& function) {
    std::string const fixed_path = table_stream(table, "FixedData");
    std::vector<std::uint8_t> const old_fixed = before.read_stream(fixed_path);
    std::vector<std::uint8_t> const new_fixed = after.read_stream(fixed_path);
    if (old_fixed.size() != new_fixed.size()) {
        throw Error(function + ": " + fixed_path + " changed size, " +
                    std::to_string(old_fixed.size()) + " -> " +
                    std::to_string(new_fixed.size()) + " bytes");
    }
    for (std::size_t i = 0; i < old_fixed.size(); ++i) {
        if (old_fixed[i] == new_fixed[i]) {
            continue;
        }
        bool allowed = false;
        for (std::size_t const row : constraint_rows) {
            std::size_t const begin =
                task_row_offset(row) + task_row::kConstraintType;
            std::size_t const end =
                task_row_offset(row) + task_row::kConstraintDate + 4;
            if (i >= begin && i < end) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            throw Error(function + ": byte " + std::to_string(i) + " of " +
                        fixed_path +
                        " changed outside the constraint words of the edited "
                        "rows");
        }
    }

    std::string const f2_meta_path = table_stream(table, "Fixed2Meta");
    if (before.has_stream(f2_meta_path) != after.has_stream(f2_meta_path)) {
        throw Error(function + ": " + f2_meta_path + " appeared or vanished");
    }
    std::vector<std::size_t> wanted = fixed2meta_offsets;
    std::sort(wanted.begin(), wanted.end());
    std::vector<std::size_t> changed;
    if (before.has_stream(f2_meta_path)) {
        std::vector<std::uint8_t> const old_f2 = before.read_stream(f2_meta_path);
        std::vector<std::uint8_t> const new_f2 = after.read_stream(f2_meta_path);
        if (old_f2.size() != new_f2.size()) {
            throw Error(function + ": " + f2_meta_path + " changed size");
        }
        for (std::size_t i = 0; i < old_f2.size(); ++i) {
            if (old_f2[i] != new_f2[i]) {
                changed.push_back(i);
            }
        }
    }
    if (changed != wanted) {
        throw Error(function + ": " + f2_meta_path + " changed bytes [" +
                    join_offsets(changed) +
                    "], expected the manual-mode flag bytes [" +
                    join_offsets(wanted) + "]");
    }
    require_streams_unchanged(before, after, {fixed_path, f2_meta_path}, function);
}

/*
 * save_plan's byte contract: FixedData may change only inside the planned rows,
 * Fixed2Data only inside their 64-byte records, and no other stream at all.
 */
void require_only_planned_row_bytes(CfbDocument const& before,
                                    CfbDocument const& after,
                                    std::vector<std::size_t> const& rows,
                                    std::string_view table,
                                    std::string const& function) {
    std::string const fixed_path = table_stream(table, "FixedData");
    std::vector<std::uint8_t> const old_fixed = before.read_stream(fixed_path);
    std::vector<std::uint8_t> const new_fixed = after.read_stream(fixed_path);
    if (old_fixed.size() != new_fixed.size()) {
        throw Error(function + ": " + fixed_path + " changed size, " +
                    std::to_string(old_fixed.size()) + " -> " +
                    std::to_string(new_fixed.size()) + " bytes");
    }
    for (std::size_t i = 0; i < old_fixed.size(); ++i) {
        if (old_fixed[i] == new_fixed[i]) {
            continue;
        }
        bool allowed = false;
        for (std::size_t const row : rows) {
            std::size_t const begin = task_row_offset(row);
            std::size_t const end = begin + task_row::kStride;
            if (i >= begin && i < end) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            throw Error(function + ": byte " + std::to_string(i) + " of " +
                        fixed_path + " changed outside the planned rows");
        }
    }

    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    bool const had_fixed2 = before.has_stream(fixed2_path);
    if (had_fixed2 != after.has_stream(fixed2_path)) {
        throw Error(function + ": " + fixed2_path + " appeared or vanished");
    }
    if (had_fixed2) {
        std::vector<std::uint8_t> const old_fixed2 = before.read_stream(fixed2_path);
        std::vector<std::uint8_t> const new_fixed2 = after.read_stream(fixed2_path);
        if (old_fixed2.size() != new_fixed2.size()) {
            throw Error(function + ": " + fixed2_path + " changed size, " +
                        std::to_string(old_fixed2.size()) + " -> " +
                        std::to_string(new_fixed2.size()) + " bytes");
        }
        for (std::size_t i = 0; i < old_fixed2.size(); ++i) {
            if (old_fixed2[i] == new_fixed2[i]) {
                continue;
            }
            bool allowed = false;
            for (std::size_t const row : rows) {
                if (row == 0) {
                    continue; /* the project row has no Fixed2Data record */
                }
                std::size_t const begin = fixed2_record_offset(row);
                std::size_t const end = begin + fixed2_row::kStride;
                if (i >= begin && i < end) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                throw Error(function + ": byte " + std::to_string(i) + " of " +
                            fixed2_path + " changed outside the planned rows");
            }
        }
    }
    require_streams_unchanged(before, after, {fixed_path, fixed2_path}, function);
}

/*
 * Re-open the file just written and prove it holds exactly what was written.
 * Returns the document parsed from disk; callers re-read rows out of it.
 */
CfbDocument re_open_and_check(CfbDocument const& written,
                              std::string const& path,
                              std::string_view table,
                              std::string const& function) {
    CfbDocument const reread = CfbDocument::from_file(path);
    if (reread.to_bytes() != written.to_bytes()) {
        throw Error(function + ": " + path +
                    " does not hold the bytes that were written");
    }
    std::string const cfb_problem = reread.check();
    if (!cfb_problem.empty()) {
        throw Error(function + ": the written file's CFB self-check failed: " +
                    cfb_problem);
    }
    std::string const table_problem = check_table(reread, table);
    if (!table_problem.empty()) {
        throw Error(function + ": the written file's task table is "
                    "inconsistent: " + table_problem);
    }
    return reread;
}

/* Resolve a uid, naming the uid in the error (row_for_uid only names the id). */
std::size_t row_for_uid_or_throw(TaskTable const& table, std::uint32_t uid,
                                 std::string const& function) {
    try {
        return table.row_for_uid(uid);
    } catch (Error const& e) {
        throw Error(function + ": " + uid_label(uid) + ": " + e.what() +
                    " -- the uid must exist in the task table (a date edit is "
                    "addressed by UniqueID, not by row index)");
    }
}

} /* namespace */

SaveResult save_date_edits(CfbDocument& doc, std::vector<DateEdit> const& edits,
                           std::string const& path, std::string_view table) {
    std::string const function = "save_date_edits";
    if (edits.empty()) {
        throw Error(function + ": the edit list is empty; there is nothing to "
                    "apply.  Pass the edits you want, or use save_plan to write "
                    "a whole schedule.");
    }

    /* ---- 1. validate everything against the current document ------------ */
    TaskTable const source_table = TaskTable::from_document(doc, table);
    std::vector<std::uint32_t> const meta_offsets =
        fixed_meta_offsets(doc, table, function);
    std::vector<std::size_t> rows;
    std::vector<std::uint32_t> seen;
    std::vector<TaskConstraint> previous;
    std::vector<ManualState> manual;
    rows.reserve(edits.size());
    seen.reserve(edits.size());
    previous.reserve(edits.size());
    manual.reserve(edits.size());
    for (DateEdit const& edit : edits) {
        if (edit.uid == 0) {
            throw Error(function + ": uid 0 is the project row (row 0) and has "
                        "no task start to pin; address a real task uid");
        }
        if (!is_date(edit.start)) {
            throw Error(function + ": " + uid_label(edit.uid) +
                        ": start " + format_date(edit.start) +
                        " is not a valid packed date (0 and 0xFFFFFFFF mean "
                        "'not stored')");
        }
        if (contains(seen, edit.uid)) {
            throw Error(function + ": " + uid_label(edit.uid) +
                        " appears twice in the edit list; one start per task");
        }
        std::size_t const row = row_for_uid_or_throw(source_table, edit.uid, function);
        if (row == 0) {
            throw Error(function + ": " + uid_label(edit.uid) +
                        " resolves to row 0, the project row; its start cannot "
                        "be pinned with a task constraint");
        }
        std::uint32_t const row_offset =
            static_cast<std::uint32_t>(task_row_offset(row));
        if (std::find(meta_offsets.begin(), meta_offsets.end(), row_offset) ==
            meta_offsets.end()) {
            throw Error(function + ": " + uid_label(edit.uid) + " (row " +
                        std::to_string(row) +
                        ") has no FixedMeta entry; Project treats a stored row "
                        "that no entry names as deleted, so the constraint "
                        "would be ignored.  Such a row cannot be scheduled.");
        }
        seen.push_back(edit.uid);
        rows.push_back(row);
        previous.push_back(read_task_constraint(doc, row, table));
        manual.push_back(read_manual_state(doc, row, table, function));
    }
    check_output_path(path, function);

    /* Manual rows cannot be scheduled by SNET alone (measured: the constraint
     * is stored and ignored).  Clearing Fixed2Meta's manual bit at the same
     * time made Project report Manual=False and honour the constraint
     * (w37-snet-auto.mpp, coordinator open 2026-10-22), so a date edit is
     * "pin + clear the mode bit".  Rows whose flag byte cannot be read are
     * pinned without conversion and warned about below. */
    std::vector<std::size_t> fixed2meta_offsets;
    for (ManualState const& state : manual) {
        if (state.known && state.manual) {
            fixed2meta_offsets.push_back(state.offset);
        }
    }

    /* ---- 2. apply to a copy (nothing is saved if a pin refuses) ---------- */
    CfbDocument work = doc;
    if (!fixed2meta_offsets.empty()) {
        std::string const f2_meta_path = table_stream(table, "Fixed2Meta");
        std::vector<std::uint8_t> f2_meta = work.read_stream(f2_meta_path);
        for (std::size_t const offset : fixed2meta_offsets) {
            f2_meta[offset] = static_cast<std::uint8_t>(f2_meta[offset] &
                                                        ~kManualFlag);
        }
        work.replace_stream(f2_meta_path, f2_meta);
    }
    for (std::size_t i = 0; i < edits.size(); ++i) {
        pin_task_start(work, rows[i], edits[i].start, table);
    }

    /* ---- 3. only the intended bytes moved -------------------------------- */
    require_only_edit_bytes(doc, work, rows, fixed2meta_offsets, table, function);
    std::string const table_problem = check_table(work, table);
    if (!table_problem.empty()) {
        throw Error(function + ": the task table is inconsistent after the "
                    "edits: " + table_problem);
    }

    /* ---- 4. save --------------------------------------------------------- */
    work.save(path);

    /* ---- 5. prove it from the bytes on disk ------------------------------ */
    CfbDocument const reread = re_open_and_check(work, path, table, function);
    TaskTable const on_disk = TaskTable::from_document(reread, table);
    SaveResult result;
    result.rows_written = edits.size();
    for (std::size_t i = 0; i < edits.size(); ++i) {
        DateEdit const& edit = edits[i];
        std::size_t const row = row_for_uid_or_throw(on_disk, edit.uid, function);
        TaskConstraint const constraint = read_task_constraint(reread, row, table);
        if (constraint.type != kConstraintSnet || constraint.date != edit.start) {
            throw Error(function + ": " + uid_label(edit.uid) +
                        ": the file on disk reads back ConstraintType " +
                        std::to_string(constraint.type) + ", ConstraintDate " +
                        format_date(constraint.date) + "; expected SNET (" +
                        std::to_string(kConstraintSnet) + "), " +
                        format_date(edit.start));
        }
        TaskValues const written = read_task_values(work, rows[i], table);
        TaskValues const on_disk_values = read_task_values(reread, row, table);
        if (on_disk_values != written) {
            throw Error(function + ": " + uid_label(edit.uid) +
                        ": re-reading the file on disk did not give the row "
                        "that was written (the other row words moved)");
        }
        if (previous[i].type != 0 && previous[i].type != kConstraintSnet) {
            result.warnings.push_back(
                uid_label(edit.uid) + ": replaced constraint type " +
                std::to_string(previous[i].type) + " (" +
                format_date(previous[i].date) + ") with SNET " +
                format_date(edit.start));
        }
        if (manual[i].known && manual[i].manual) {
            ManualState const after =
                read_manual_state(reread, row, table, function);
            if (!after.known || after.manual) {
                throw Error(function + ": " + uid_label(edit.uid) +
                            ": the file on disk still stores the row as "
                            "manually scheduled; the mode bit did not clear");
            }
            result.warnings.push_back(
                uid_label(edit.uid) +
                ": was manual; written as SNET and converted to auto-scheduled "
                "so Project honours the date (manual scheduling is not "
                "preserved, measured 2026-10-22)");
        } else if (!manual[i].known) {
            result.warnings.push_back(
                uid_label(edit.uid) +
                ": scheduling mode unknown (the Fixed2Meta flag byte is "
                "unreadable); SNET is honoured only for an auto-scheduled row");
        }
        result.warnings.push_back("re-read from disk: " + uid_label(edit.uid) +
                                  " SNET " + format_date(edit.start) +
                                  " at ConstraintType 4, row " +
                                  std::to_string(row) + " of " + path);
    }

    doc = work;
    return result;
}

SaveResult save_plan(CfbDocument& doc, std::vector<PlannedRow> const& rows,
                     std::string const& path, std::string_view table) {
    std::string const function = "save_plan";

    /* ---- 1. validate every planned row against the current document ------ */
    TaskTable const source_table = TaskTable::from_document(doc, table);
    std::vector<std::size_t> row_index;
    std::vector<TaskValues> expected;
    std::vector<std::uint32_t> seen;
    row_index.reserve(rows.size());
    expected.reserve(rows.size());
    seen.reserve(rows.size());
    for (PlannedRow const& planned : rows) {
        if (contains(seen, planned.uid)) {
            throw Error(function + ": " + uid_label(planned.uid) +
                        " appears twice in the plan; one row per uid");
        }
        std::size_t const row =
            row_for_uid_or_throw(source_table, planned.uid, function);
        TaskValues resolved;
        try {
            resolved = resolve_task_values(doc, row, planned.values, table);
        } catch (Error const& e) {
            throw Error(function + ": " + uid_label(planned.uid) + ": " +
                        e.what());
        }
        seen.push_back(planned.uid);
        row_index.push_back(row);
        expected.push_back(resolved);
    }
    check_output_path(path, function);

    /* ---- 2. write the plan to a copy ------------------------------------- */
    CfbDocument work = doc;
    std::vector<std::string> warnings;
    std::size_t const written =
        recalculate_write_path(work, rows, table, &warnings);
    if (written != rows.size()) {
        throw Error(function + ": recalculate_write_path wrote " +
                    std::to_string(written) + " of " +
                    std::to_string(rows.size()) + " rows");
    }

    /* ---- 3. only the planned rows moved ---------------------------------- */
    require_only_planned_row_bytes(doc, work, row_index, table, function);

    /* ---- 4. save --------------------------------------------------------- */
    work.save(path);

    /* ---- 5. prove it from the bytes on disk ------------------------------ */
    CfbDocument const reread = re_open_and_check(work, path, table, function);
    TaskTable const on_disk = TaskTable::from_document(reread, table);
    SaveResult result;
    result.rows_written = written;
    result.warnings = std::move(warnings);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        PlannedRow const& planned = rows[i];
        std::size_t const row = row_for_uid_or_throw(on_disk, planned.uid, function);
        TaskConstraint const constraint = read_task_constraint(reread, row, table);
        if (constraint.type !=
                static_cast<std::uint16_t>(expected[i].constraint_type) ||
            constraint.date != expected[i].constraint_date) {
            throw Error(function + ": " + uid_label(planned.uid) +
                        ": the file on disk reads back ConstraintType " +
                        std::to_string(constraint.type) + ", ConstraintDate " +
                        format_date(constraint.date) +
                        ", not the written values");
        }
        TaskValues const on_disk_values = read_task_values(reread, row, table);
        if (on_disk_values != expected[i]) {
            throw Error(function + ": " + uid_label(planned.uid) +
                        ": re-reading the file on disk did not give the row "
                        "that was written");
        }
    }
    result.warnings.push_back("re-read from disk: " +
                              std::to_string(rows.size()) +
                              " row(s) match the written values (CFB self-check "
                              "and table consistency passed) in " + path);

    doc = work;
    return result;
}

} /* namespace mppwrite */
