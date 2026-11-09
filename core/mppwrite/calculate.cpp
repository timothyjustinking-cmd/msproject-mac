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
#include <map>
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

bool calculate_measured_finish(std::uint32_t start, std::uint32_t duration,
                               std::uint32_t& finish) {
    constexpr int kDayStartSeconds = 8 * 3600;
    constexpr int kDayFinishSeconds = 17 * 3600;
    constexpr std::uint32_t kDayTicks = 4800; /* 8 hours */
    constexpr int kSaturday = 0;              /* day 0 (1983-12-31) was a Saturday */
    constexpr int kSunday = 1;
    if (!is_date(start) || duration == 0 || duration % kDayTicks != 0) {
        return false;
    }
    PackedDate const first = unpack_date(start);
    int const weekday = first.day % 7;
    if (first.seconds_of_day != kDayStartSeconds || weekday == kSaturday ||
        weekday == kSunday) {
        return false;
    }
    std::uint32_t remaining = duration / kDayTicks - 1;
    if (remaining > 0xFFFFu) {
        return false;
    }
    PackedDate last = first;
    while (remaining > 0) {
        ++last.day;
        int const next = last.day % 7;
        if (next != kSaturday && next != kSunday) {
            --remaining;
        }
    }
    if (last.day > 0xFFFF) {
        return false;
    }
    last.seconds_of_day = kDayFinishSeconds;
    finish = pack_date(last);
    return true;
}

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

/* W35b-0.5 (R35G1-3): the parameters the pair does not vary.  Every table the
 * save leaves untouched is pinned to the measured control's shape, so a file
 * that differs in a way no pair measured is refused rather than saved with
 * values the port cannot derive. */
constexpr std::size_t kRscHeader = 48;
constexpr std::size_t kRscRow = 172;
constexpr std::size_t kCalHeader = 64;
constexpr std::size_t kCalRow = 12;
constexpr std::size_t kF2MetaHeader = 16;
constexpr std::size_t kF2MetaEntry = 96;
constexpr std::size_t kF2MetaFlags = 8;
constexpr std::uint8_t kF2MetaActive = 0x40;
constexpr std::uint32_t kCalNameKey = 0x0D400001u;

bool is_baseline_key(std::uint32_t key) {
    switch (key) {
        /* Task work, cost, duration, start, finish. */
        case 0x0B400001u: case 0x0B400006u: case 0x0B40001Bu:
        case 0x0B40002Bu: case 0x0B40002Cu:
        /* Resource work, cost. */
        case 0x0C40000Fu: case 0x0C400011u:
        /* Assignment work, cost, start, finish. */
        case 0x0F400010u: case 0x0F400020u: case 0x0F400092u: case 0x0F400093u:
            return true;
        default:
            return false;
    }
}

/* A table's VarMeta: no baseline block, and per uid exactly the measured keys
 * (a note, calendar, deadline, task-type or resource block changes the shape).
 * `zero` is the key set of uid 0 when it has its own, `row` that of every other
 * uid; `groups` is how many uids store anything. */
void check_var_keys(CfbDocument const& doc, std::string const& table,
                    std::string const& label,
                    std::vector<std::uint32_t> const* zero,
                    std::vector<std::uint32_t> const& row, std::size_t groups) {
    std::vector<VarEntry> const entries =
        read_var_entries(doc.read_stream("114/" + table + "/VarMeta"));
    std::map<std::uint32_t, std::vector<std::uint32_t>> by_uid;
    for (VarEntry const& entry : entries) {
        if (is_baseline_key(entry.field_id)) {
            throw Error(label + " uid " + std::to_string(entry.mid) +
                        " carries a baseline (" + hex(entry.field_id) +
                        "); the measured Calculate Project save has none");
        }
        by_uid[entry.mid].push_back(entry.field_id);
    }
    for (auto& [uid, keys] : by_uid) {
        std::sort(keys.begin(), keys.end());
        std::vector<std::uint32_t> const& want = (uid == 0 && zero != nullptr) ? *zero : row;
        if (keys != want) {
            throw Error(label + " uid " + std::to_string(uid) +
                        " stores variable records other than the measured "
                        "ones; the measured Calculate Project save stores "
                        "only the keys of its control");
        }
    }
    if (by_uid.size() != groups) {
        throw Error(label + " table stores variable records for " +
                    std::to_string(by_uid.size()) + " rows; the measured "
                    "Calculate Project save stores them for " +
                    std::to_string(groups));
    }
}

std::vector<std::uint32_t> const kProjectTaskKeys = {
    0x0B40000Eu, 0x0B4004E4u, 0x0B400563u, 0x0B400564u};
std::vector<std::uint32_t> const kLeafTaskKeys = {
    0x0B40000Eu, 0x0B4000A4u, 0x0B4000A5u, 0x0B4000A8u, 0x0B4000A9u,
    0x0B4000AAu, 0x0B4004E5u, 0x0B400563u, 0x0B400564u};
std::vector<std::uint32_t> const kPlaceholderAssignmentKeys = {
    0x0F400031u, 0x0F40027Au, 0x0F400299u};
std::vector<std::uint32_t> const kNullResourceKeys = {0x0C4002D6u, 0x0C4002E3u};

void check_unvaried_parameters(CfbDocument const& doc, TaskTable const& rows) {
    /* The Resource Sheet holds only the null row; the assignments name
     * Project's placeholder. */
    std::size_t const rsc_size = doc.read_stream("114/TBkndRsc/FixedData").size();
    if (rsc_size != kRscHeader + kRscRow) {
        throw Error("the resource table has " +
                    std::to_string(rsc_size >= kRscHeader
                                       ? (rsc_size - kRscHeader) / kRscRow
                                       : 0) +
                    " rows (" + std::to_string(rsc_size) +
                    " bytes); the measured Calculate Project save has only "
                    "Project's null resource row, no resources on the "
                    "Resource Sheet");
    }
    /* The Standard calendar with no working-time or exception block of its own. */
    std::size_t const cal_size = doc.read_stream("114/TBkndCal/FixedData").size();
    std::vector<VarEntry> const cal_entries =
        read_var_entries(doc.read_stream("114/TBkndCal/VarMeta"));
    bool standard = cal_size == kCalHeader + 2 * kCalRow &&
                    cal_entries.size() == 1 && cal_entries[0].mid == 1 &&
                    cal_entries[0].field_id == kCalNameKey;
    if (standard) {
        std::vector<std::uint8_t> const name = read_var_block(
            doc.read_stream("114/TBkndCal/Var2Data"), cal_entries[0].offset);
        static std::vector<std::uint8_t> const want = {
            'S', 0, 't', 0, 'a', 0, 'n', 0, 'd', 0, 'a', 0, 'r', 0, 'd', 0, 0, 0};
        standard = name == want;
    }
    if (!standard) {
        throw Error("the calendar table is not the measured Standard calendar "
                    "alone (named Standard, with no working-time or exception "
                    "block of its own); the measured Calculate Project save "
                    "has no other calendar");
    }
    check_var_keys(doc, "TBkndTask", "task", &kProjectTaskKeys, kLeafTaskKeys, kRows);
    check_var_keys(doc, "TBkndAssn", "assignment", nullptr,
                   kPlaceholderAssignmentKeys, kRows - 1);
    check_var_keys(doc, "TBkndRsc", "resource", &kNullResourceKeys, kNullResourceKeys, 1);
    /* Active tasks with the measured FixedMeta flags (summary, effort-driven,
     * task-type and presence bits), which the pair never varies. */
    std::vector<std::uint8_t> const f2_meta =
        doc.read_stream(std::string(kTable) + "/Fixed2Meta");
    std::vector<std::uint8_t> const meta =
        doc.read_stream(std::string(kTable) + "/FixedMeta");
    static std::uint8_t const kLeafMeta[kFixedMetaEntry] = {
        0x00, 0x00, 0x09, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x07, 0x48, 0xEF, 0x1F, 0x10};
    for (std::size_t row = 1; row < kRows; ++row) {
        std::size_t const f2_at = kF2MetaHeader + (row + 3) * kF2MetaEntry;
        if (f2_meta.size() < f2_at + kF2MetaEntry ||
            (f2_meta[f2_at + kF2MetaFlags] & kF2MetaActive) == 0) {
            throw Error("task uid " + std::to_string(rows.uid(row)) +
                        " is inactive; the measured Calculate Project save "
                        "has active tasks only");
        }
        std::size_t const at = kFixedMetaHeader + (row + 3) * kFixedMetaEntry;
        for (std::size_t i = 0; i < kFixedMetaEntry; ++i) {
            if (i >= 4 && i < 16) {
                continue; /* the row's offset and stamp */
            }
            if (meta[at + i] != kLeafMeta[i]) {
                throw Error("task uid " + std::to_string(rows.uid(row)) +
                            "'s FixedMeta metadata flags differ from the "
                            "measured entry at byte " + std::to_string(i) +
                            " (summary, effort-driven, task-type and presence "
                            "bits)");
            }
        }
    }
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
            /* W35b-0.2 (R35G1-1): a Deadline limits the backward pass; the pair
             * has none and the port's slack and late dates ignore it. */
            if (is_date(rows.u32(row, task_row::kDeadline))) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " carries a deadline; the measured "
                                       "Calculate Project save has none");
            }
            /* W35b-0.3 (R35G1-2): the writer stamps Fixed2Data +46 with the Props
             * project start, which is right only for a leaf that starts at the
             * project start; the linked and lag pairs store the predecessor-finish
             * bound there. */
            if (rows.u32(row, task_row::kStart) != rows.u32(0, task_row::kStart)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " starts at a date other than the project "
                                       "start; the measured Calculate Project "
                                       "save has every leaf starting at the "
                                       "project start");
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
        /* W35b-2.3 (R35G1-6): the measured save is the FIRST calculation of an
         * automatically built file.  A file that already holds the five overtime
         * records on its project row was calculated before (by Project or by this
         * port) and edited again; Project's repeat save is unmeasured, and this
         * writer's append would collide with them, so it is refused up front.  (A
         * calculated file that is not pending is refused above, as not pending.) */
        for (VarEntry const& entry :
             read_var_entries(doc.read_stream(std::string(kTable) + "/VarMeta"))) {
            if (entry.mid == 0 && (entry.field_id == kTaskActualOvertimeWorkFieldId ||
                                   entry.field_id == kTaskRemainingOvertimeWorkFieldId ||
                                   entry.field_id == kTaskOvertimeCostFieldId ||
                                   entry.field_id == kTaskActualOvertimeCostFieldId ||
                                   entry.field_id == kTaskRemainingOvertimeCostFieldId)) {
                return fail(error, "the project row already holds the overtime record " +
                                       hex(entry.field_id) +
                                       ", so the file has been calculated before; the "
                                       "measured Calculate Project save is the first "
                                       "calculation of an automatically built file");
            }
        }
        /* W35b-2.1 (R35G1-4): the two other Props records the save writes are
         * checked here, before any write, so no append can land in a target that
         * a later Props edit would then refuse. */
        std::uint32_t finish_length = 0;
        std::size_t const finish_at =
            props_payload(props, kPropProjectFinish, finish_length);
        if (finish_at == static_cast<std::size_t>(-1) || finish_length != 4 ||
            get_u32(props, finish_at - 4) != kPropsRecordKindDate) {
            return fail(error, "114/Props has no 4-byte date record for the "
                               "project finish (0x02400003), so the save cannot "
                               "move it");
        }
        std::uint32_t start_length = 0;
        std::size_t const start_at =
            props_payload(props, kPropProjectStart, start_length);
        if (start_at == static_cast<std::size_t>(-1) || start_length != 4) {
            return fail(error, "114/Props has no 4-byte project start "
                               "(0x02400002), so the leaf Fixed2Data +46 cannot "
                               "be set");
        }
        /* W35b-0.3: the pair lengthens the project; no pair shortens it. */
        if (shape.project_finish < rows.u32(0, task_row::kFinish)) {
            return fail(error, "the recalculation would shorten the project (the "
                               "latest task finish is before the stored project "
                               "finish); the measured Calculate Project save "
                               "lengthens it");
        }
        if (rows.u32(0, task_row::kFinish) == shape.project_finish) {
            return fail(error, "the project finish already equals the latest "
                               "task finish; there is nothing to calculate");
        }
        /* W35b-0.3: the stored project finish is the one before the change, so a
         * leaf that finishes before it had slack and one that finishes at it ended
         * the project.  The measured pair has one leaf past that finish and the
         * other before it, so Project's critical flags do not change.  A second
         * leaf at or past it (the lengthened leaf overtaken, or the old critical
         * leaf left behind) moves the critical path, and the writer never writes a
         * critical flag.  (The reader's flag is stored total slack <= 0, which is
         * stale here, so it cannot be compared.) */
        for (std::size_t row = 1; row < kRows; ++row) {
            if (rows.uid(row) != shape.critical_uid &&
                rows.u32(row, task_row::kFinish) >= rows.u32(0, task_row::kFinish)) {
                return fail(error, "the recalculation moves the critical path (task "
                                   "uid " + std::to_string(rows.uid(row)) +
                                       " does not finish before the stored project "
                                       "finish, as the other leaf must); the "
                                       "measured Calculate Project save leaves the "
                                       "critical flags as they were");
            }
        }
        /* W35b-0.4 (RW35-W2): the pair changes one Duration, so every leaf's
         * early dates are already final: Finish is Start plus Duration on the
         * measured calendar.  A file left pending by a Start, Finish, progress or
         * calendar edit fails this, and the writer would save the old dates. */
        for (std::size_t row = 1; row < kRows; ++row) {
            std::uint32_t expected = 0;
            if (!calculate_measured_finish(rows.u32(row, task_row::kStart),
                                           rows.u32(row, task_row::kDuration), expected) ||
                expected != rows.u32(row, task_row::kFinish)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       "'s finish is not its start plus its duration "
                                       "on the measured calendar (Monday to Friday, "
                                       "08:00 to 17:00, whole 8-hour days), so the "
                                       "pending recalculation was not caused by a "
                                       "Duration edit alone");
            }
        }
        /* W35b-0.5 (R35G1-3): the parameters no pair varies. */
        check_unvaried_parameters(doc, rows);
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
        /* W35b-2.2 (R35G1-5): the unlinked backward pass gives every leaf the project
         * finish as its late finish (spec/writers/calculate.md section 8), and the leaf
         * that ends the project has no slack and starts late where it starts early. */
        if (leaf.late_finish != request.project_finish) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has a late finish before the project finish; the unlinked "
                        "backward pass gives every leaf the project finish");
        }
        if (leaf.uid == shape.critical_uid &&
            (leaf.total_slack != 0 || leaf.late_start != start)) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " ends the project, so its slack is 0 and its late start is its "
                        "early start");
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

void validate_calculate(CfbDocument const& doc, CalculateRequest const& request) {
    CalculateShape shape;
    std::string reason;
    if (!read_calculate_shape(doc, shape, reason)) {
        throw Error("calculate: " + reason);
    }
    TaskTable const before = TaskTable::from_document(doc, kTable);
    check_request(shape, before, request);
}

CalculateWrite save_calculate(CfbDocument& doc, CalculateRequest const& request,
                              std::string const& path) {
    if (path.empty() || std::filesystem::path(path).filename().empty()) {
        throw Error("calculate: the output path is empty or names a directory");
    }
    validate_calculate(doc, request);
    TaskTable const before = TaskTable::from_document(doc, kTable);

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
    /* W35b-2.1 (R35G1-4): save_var_record and save_props_edit each write the path they are
     * given, so the chain runs against a private temporary file beside the target and the
     * target is written once, atomically, after the whole result is proved.  A refusal
     * anywhere leaves the target exactly as it was. */
    std::string const scratch = path + ".calculate.tmp";
    std::error_code ignored;
    std::filesystem::remove(scratch, ignored);
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
            save_var_record(work, kVarTaskTable, {edit}, scratch);
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
        save_props_edit(work, props_edit, scratch);

        work.save(scratch);
        CfbDocument const reread = CfbDocument::from_file(scratch);
        if (reread.to_bytes() != work.to_bytes()) {
            throw Error("calculate: the written file does not re-read to the "
                        "bytes built");
        }
        reread.save(path);
        doc = reread;
    } catch (...) {
        std::filesystem::remove(scratch, ignored);
        throw;
    }
    std::filesystem::remove(scratch, ignored);
    result.leaves_written = request.leaves.size();
    return result;
}

} /* namespace mppwrite */
