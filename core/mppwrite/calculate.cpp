/*
 * calculate.cpp -- the measured Calculate Project save for the unlinked
 * two-leaf shape.  The rules and the evidence are in calculate.h.
 */

#include "calculate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "actuals.h"
#include "edit_marker.h"
#include "props_edit.h"
#include "row_writer.h"
#include "summary_info.h"
#include "table.h"
#include "task_mode.h"
#include "varmeta_edit.h"

namespace mppwrite {
namespace {

constexpr char const* kTable = "114/TBkndTask";
constexpr std::size_t kRows = 3; /* the project row and two leaves */
constexpr std::uint32_t kNoDate = 0xFFFFFFFFu;

/* FixedData words this save moves (row_writer.h names the rest). */
constexpr std::size_t kFreeSlack = 74;
constexpr std::size_t kStartSlack = 186;
constexpr std::size_t kTotalSlack = 190;

/* 114/Props records (ids stored little-endian, 0x40 in the third byte). */
constexpr std::uint32_t kPropProjectStart = 0x02400002u;
constexpr std::uint32_t kPropProjectFinish = 0x02400003u;
constexpr std::uint32_t kPropCalculationPending = 0x02400024u;
constexpr std::uint32_t kPropsRecordKindDate = 4;
constexpr std::uint32_t kPropsRecordKindFlag = 2;

/* Fixed2Data stamps (record = row + 3). */
constexpr std::size_t kFixed2Stamp = 42;
constexpr std::size_t kFixed2ProjectDuration = 58;

/* FixedMeta entry 3 is the project row (row + 3 is not needed: the entry
 * order is the row order after the three header entries). */
constexpr std::size_t kFixedMetaHeader = 16;
constexpr std::size_t kFixedMetaEntry = 47;
constexpr std::size_t kMetaPresenceLow = 18;
constexpr std::size_t kMetaPresenceHigh = 19;

std::uint32_t get_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[o + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08X", value);
    return text;
}

/* Offset of the PAYLOAD of the one 114/Props record with this id, or SIZE_MAX
 * when absent.  The chain is [u32 length][u32 id][u32 kind][payload] from
 * offset 16; a duplicate is a shape no file has. */
std::size_t props_payload(std::vector<std::uint8_t> const& props,
                          std::uint32_t id, std::uint32_t& length_out) {
    std::size_t found = static_cast<std::size_t>(-1);
    std::size_t at = 16;
    while (at + 12 <= props.size()) {
        std::uint32_t const length = get_u32(props, at);
        std::uint32_t const stored = get_u32(props, at + 4);
        if ((stored & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (static_cast<std::uint64_t>(at) + 12u + length > props.size()) {
            throw Error("a 114/Props record runs past the stream");
        }
        if (stored == id) {
            if (found != static_cast<std::size_t>(-1)) {
                throw Error("114/Props holds more than one record for " +
                            hex(id));
            }
            found = at + 12;
            length_out = length;
        }
        at += 12u + length;
    }
    return found;
}

bool fail(std::string& error, std::string const& message) {
    error = message;
    return false;
}

std::string props_path() {
    return kPropsBackEnd;
}

} /* namespace */

bool read_calculate_shape(CfbDocument const& doc, CalculateShape& shape,
                          std::string& error) {
    shape = CalculateShape{};
    error.clear();
    try {
        TaskTable const rows = TaskTable::from_document(doc, kTable);
        if (rows.rows() != kRows) {
            return fail(error, "the file has " + std::to_string(rows.rows()) +
                                   " task rows; the measured Calculate Project "
                                   "save has the project row and exactly two "
                                   "leaf tasks");
        }
        if (rows.uid(0) != 0) {
            return fail(error, "row 0 is not the project row");
        }
        /* Whole-file shape: unlinked (empty link table), automatic, top-level
         * leaves with no progress, no task calendar, one placeholder
         * assignment per task, no manual rows.  A linked, lagged or
         * constrained file is refused here (the link table is non-empty) or
         * by the constraint check below. */
        std::string reason;
        if (!read_task_file_shape(doc, reason, kTable)) {
            return fail(error, reason);
        }
        std::vector<std::uint8_t> const meta =
            doc.read_stream(std::string(kTable) + "/FixedMeta");
        for (std::size_t row = 1; row < kRows; ++row) {
            std::size_t const entry_at =
                kFixedMetaHeader + (row + 3) * kFixedMetaEntry;
            if (task_is_manually_scheduled(doc, row, kTable)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " is manually scheduled; the measured "
                                       "Calculate Project save has automatic "
                                       "rows only");
            }
            if (meta.size() < entry_at + kFixedMetaEntry ||
                meta[entry_at + 17] != 0x48 || meta[entry_at + 18] != 0xEF) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " does not store the automatic row's "
                                       "0x48/0xef at FixedMeta +17/+18");
            }
            if (rows.u16(row, task_row::kConstraintType) != 0) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " carries a constraint; the measured "
                                       "Calculate Project save has none");
            }
            shape.leaf_uids.push_back(rows.uid(row));
        }
        std::uint32_t latest = 0;
        for (std::size_t row = 1; row < kRows; ++row) {
            std::uint32_t const finish = rows.u32(row, task_row::kFinish);
            if (finish >= latest) {
                latest = finish;
                shape.critical_uid = rows.uid(row);
                shape.critical_duration = rows.u32(row, task_row::kDuration);
            }
        }
        shape.project_start = rows.u32(0, task_row::kStart);
        shape.project_finish = latest;
        std::vector<std::uint8_t> const props = doc.read_stream(props_path());
        std::uint32_t length = 0;
        std::size_t const flag =
            props_payload(props, kPropCalculationPending, length);
        if (flag == static_cast<std::size_t>(-1) || length != 2) {
            return fail(error, "114/Props has no 0x02400024 calculation-"
                               "pending record, so the file cannot be a "
                               "pending recalculation");
        }
        if (props[flag] != 1 || props[flag + 1] != 0) {
            return fail(error, "the calculation is not pending (114/Props "
                               "0x02400024 is not 01 00); there is nothing to "
                               "calculate");
        }
        if (rows.u32(0, task_row::kFinish) == shape.project_finish) {
            return fail(error, "the project finish already equals the latest "
                               "task finish; there is nothing to calculate");
        }
        for (std::size_t row = 1; row < kRows; ++row) {
            if (rows.u32(row, task_row::kStartAnchor) == kNoDate) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " has no start anchor; the measured "
                                       "Calculate Project save starts from "
                                       "anchored leaves");
            }
        }
        return true;
    } catch (Error const& caught) {
        error = caught.what();
        return false;
    }
}

namespace {

/* The request must be consistent with the stored leaves it is applied to. */
void check_request(CalculateShape const& shape, TaskTable const& rows,
                   CalculateRequest const& request) {
    if (request.project_finish != shape.project_finish) {
        throw Error("calculate: the project finish " +
                    hex(request.project_finish) +
                    " is not the latest task finish " +
                    hex(shape.project_finish));
    }
    if (request.project_duration != shape.critical_duration) {
        throw Error("calculate: the project duration " +
                    std::to_string(request.project_duration) +
                    " is not the critical task's duration " +
                    std::to_string(shape.critical_duration));
    }
    if (request.leaves.size() != shape.leaf_uids.size()) {
        throw Error("calculate: " + std::to_string(request.leaves.size()) +
                    " leaf results were given for " +
                    std::to_string(shape.leaf_uids.size()) + " leaves");
    }
    for (std::size_t i = 0; i < request.leaves.size(); ++i) {
        CalculateLeaf const& leaf = request.leaves[i];
        if (leaf.uid != shape.leaf_uids[i]) {
            throw Error("calculate: leaf result " + std::to_string(i) +
                        " is for uid " + std::to_string(leaf.uid) +
                        ", the file's leaf is uid " +
                        std::to_string(shape.leaf_uids[i]));
        }
        std::size_t const row = rows.row_for_uid(leaf.uid);
        std::uint32_t const start = rows.u32(row, task_row::kStart);
        std::uint32_t const finish = rows.u32(row, task_row::kFinish);
        if (leaf.late_start < start || leaf.late_finish < finish) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has a late date before its early date");
        }
        if (leaf.late_finish > request.project_finish) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has a late finish after the project finish");
        }
        if (leaf.free_slack < 0 || leaf.total_slack < 0 ||
            leaf.start_slack != leaf.total_slack ||
            leaf.free_slack != leaf.total_slack) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has slack the unlinked shape cannot have (free, "
                        "start and total slack are equal and not negative)");
        }
    }
}

} /* namespace */

CalculateWrite save_calculate(CfbDocument& doc, CalculateRequest const& request,
                              std::string const& path) {
    if (path.empty() || std::filesystem::path(path).filename().empty()) {
        throw Error("calculate: the output path is empty or names a directory");
    }
    CalculateShape shape;
    std::string reason;
    if (!read_calculate_shape(doc, shape, reason)) {
        throw Error("calculate: " + reason);
    }
    TaskTable const before = TaskTable::from_document(doc, kTable);
    check_request(shape, before, request);

    CfbDocument work = doc;
    TaskTable after = before;

    /* The project row. */
    after.set_u32(0, task_row::kDuration, request.project_duration);
    after.set_u32(0, task_row::kRemainingDuration, request.project_duration);
    after.set_u32(0, task_row::kFinish, request.project_finish);
    after.set_u32(0, task_row::kFinish2, request.project_finish);
    after.set_u32(0, task_row::kFinish3, request.project_finish);
    /* The leaves. */
    for (CalculateLeaf const& leaf : request.leaves) {
        std::size_t const row = before.row_for_uid(leaf.uid);
        after.set_u32(row, kFreeSlack,
                      static_cast<std::uint32_t>(leaf.free_slack));
        after.set_u32(row, task_row::kStart3, leaf.late_start);
        after.set_u32(row, task_row::kFinish3, leaf.late_finish);
        after.set_u32(row, task_row::kStartAnchor, kNoDate);
        after.set_u32(row, kStartSlack,
                      static_cast<std::uint32_t>(leaf.start_slack));
        after.set_u32(row, kTotalSlack,
                      static_cast<std::uint32_t>(leaf.total_slack));
    }

    /* Fixed2Data: the project duration word clears, each leaf gets the
     * measured stamp and the Props project start. */
    std::vector<std::uint8_t> props = work.read_stream(props_path());
    std::uint32_t length = 0;
    std::size_t const start_at = props_payload(props, kPropProjectStart, length);
    if (start_at == static_cast<std::size_t>(-1) || length != 4) {
        throw Error("calculate: 114/Props has no 4-byte project start "
                    "(0x02400002), so the leaf Fixed2Data +46 cannot be set");
    }
    std::uint32_t const project_start = get_u32(props, start_at);
    std::vector<std::uint8_t> fixed2 = read_fixed2_data(work, kTable);
    put_u32(fixed2, fixed2_record_offset(0) + kFixed2ProjectDuration, 0);
    for (CalculateLeaf const& leaf : request.leaves) {
        std::size_t const at = fixed2_record_offset(before.row_for_uid(leaf.uid));
        fixed2[at + kFixed2Stamp] = kCalculateLeafStamp;
        put_u32(fixed2, at + fixed2_row::kStart, project_start);
    }
    work.replace_stream(std::string(kTable) + "/Fixed2Data", fixed2);

    write_project_summary(work, before, after);
    after.store_into(work, kTable);
    write_edit_marker(work);

    /* The five overtime records on the project row, one measured append each;
     * the FixedMeta presence bits Project sets are 0x40 at +18 and 0x1f at +19
     * (the pair measures the five together, so the split below is a
     * bookkeeping choice that leaves the final state exact). */
    struct Append {
        std::uint32_t field;
        std::size_t byte;
        std::uint8_t bit;
    };
    static constexpr Append kAppends[] = {
        {kTaskActualOvertimeWorkFieldId, kMetaPresenceHigh, 0x01},
        {kTaskRemainingOvertimeWorkFieldId, kMetaPresenceHigh, 0x02},
        {kTaskOvertimeCostFieldId, kMetaPresenceHigh, 0x04},
        {kTaskActualOvertimeCostFieldId, kMetaPresenceHigh, 0x08},
        {kTaskRemainingOvertimeCostFieldId, kMetaPresenceHigh, 0x10},
    };
    CalculateWrite result;
    bool const existed = std::filesystem::exists(path);
    try {
    for (Append const& append : kAppends) {
        VarRecordEdit edit;
        edit.uid = 0;
        edit.field_id = append.field;
        edit.shape = VarRecordShape::append;
        edit.payload.assign(8, 0);
        edit.presence.push_back({append.byte, append.bit, true});
        if (result.var_records_appended == 0) {
            edit.presence.push_back({kMetaPresenceLow, 0x40, true});
        }
        save_var_record(work, kVarTaskTable, {edit}, path);
        ++result.var_records_appended;
    }

    /* 114/Props: the project finish and the pending flag, in place. */
    PropsEdit props_edit;
    PropsRecordEdit finish;
    finish.id = kPropProjectFinish;
    finish.kind = kPropsRecordKindDate;
    finish.payload.resize(4);
    put_u32(finish.payload, 0, request.project_finish);
    PropsRecordEdit pending;
    pending.id = kPropCalculationPending;
    pending.kind = kPropsRecordKindFlag;
    pending.payload = {0, 0};
    props_edit.records = {finish, pending};
    save_props_edit(work, props_edit, path);

    work.save(path);
    CfbDocument const reread = CfbDocument::from_file(path);
    if (reread.to_bytes() != work.to_bytes()) {
        throw Error("calculate: the written file does not re-read to the "
                    "bytes built");
    }
    doc = reread;
    } catch (...) {
        std::error_code ignored;
        if (!existed) {
            std::filesystem::remove(path, ignored);
        }
        throw;
    }
    result.leaves_written = request.leaves.size();
    return result;
}

} /* namespace mppwrite */
